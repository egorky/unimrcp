#include "google_stt_streamer.hpp"
#include "google/cloud/speech/v1/speech_client.h"
#include "google/cloud/grpc_options.h"
#include <grpcpp/grpcpp.h>
#include <fstream> // For reading credentials file
#include <sstream> // For reading credentials file
#include <chrono>  // For sleep

// Helper to load credentials from a file
// This is a simplified version. For production, consider more robust error handling.
std::shared_ptr<grpc::ChannelCredentials> LoadCredentialsFromFile(const std::string& path) {
    if (path.empty()) {
        // Try to use Google Default Credentials if no path is provided
        // This might work if GOOGLE_APPLICATION_CREDENTIALS env var is set
        // or running on GCP with a service account attached.
        return grpc::GoogleDefaultCredentials();
    }

    std::ifstream creds_file(path);
    if (!creds_file.is_open()) {
        // In a real plugin, this error should be logged via UniMRCP's logger
        fprintf(stderr, "Failed to open credentials file: %s\n", path.c_str());
        return nullptr;
    }
    std::stringstream creds_buffer;
    creds_buffer << creds_file.rdbuf();
    std::string creds_json = creds_buffer.str();

    grpc::experimental:: souvent::ServiceAccountJWTAccessCredentialsOptions jwt_options;
    jwt_options.json_key = creds_json;
    return grpc::experimental::ServiceAccountJWTAccessCredentials(jwt_options);
}


// --- C++ Class Implementation ---

GoogleSttStreamer::GoogleSttStreamer(const std::string& credentials_path, google_stt_result_callback_t result_callback, void* user_data)
    : credentials_path_(credentials_path),
      result_callback_c_(result_callback),
      user_data_c_(user_data),
      stream_active_(false),
      reader_thread_active_(false) {

    auto channel_credentials = LoadCredentialsFromFile(credentials_path_);
    if (!channel_credentials) {
        // Error logged in LoadCredentialsFromFile
        // Consider throwing an exception or setting an error state
        // For now, speech_client_ will remain null, and operations will fail.
        fprintf(stderr, "GoogleSttStreamer: Failed to load credentials. Client will not be functional.\n");
        return;
    }
    
    // Use default options, but add our credentials.
    // The google-cloud-cpp library will create a default channel using these credentials.
    google::cloud::Options options;
    options.set<google::cloud::UnifiedCredentialsOption>(
        google::cloud::MakeServiceAccountCredentials(channel_credentials->GetJson()));

    speech_client_ = std::make_unique<google::cloud::speech::v1::SpeechClient>(
        google::cloud::speech::v1::MakeSpeechConnection(options)
    );
}

GoogleSttStreamer::~GoogleSttStreamer() {
    CloseStream(); // Ensure everything is cleaned up
}

bool GoogleSttStreamer::StartStream(const std::string& language_code, int sample_rate_hz, bool interim_results) {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (stream_active_ || reader_thread_active_) {
        fprintf(stderr, "GoogleSttStreamer::StartStream: Stream already active or reader thread running.\n");
        return false;
    }
    if (!speech_client_) {
        fprintf(stderr, "GoogleSttStreamer::StartStream: Speech client not initialized (credentials issue?).\n");
        return false;
    }
    
    context_ = std::make_shared<grpc::ClientContext>();
    // You can set metadata on the context if needed, e.g., for routing or billing
    // context_->AddMetadata("x-goog-request-params", "value");

    streamer_ = speech_client_->AsyncStreamingRecognize(context_.get());
    if (!streamer_) {
        fprintf(stderr, "GoogleSttStreamer::StartStream: Failed to create AsyncStreamingRecognize stream.\n");
        if (result_callback_c_) {
            result_callback_c_(nullptr, false, "Failed to create gRPC stream", user_data_c_);
        }
        return false;
    }

    // Start the stream. This is an asynchronous operation.
    // The future returned by Start() will be ready when the RPC is initialized.
    auto start_future = streamer_->Start();
    if (!start_future.get()) { // .get() blocks until the start operation is complete
         fprintf(stderr, "GoogleSttStreamer::StartStream: Failed to start the gRPC stream.\n");
        if (result_callback_c_) {
            result_callback_c_(nullptr, false, "Failed to start gRPC stream", user_data_c_);
        }
        streamer_.reset(); // Clean up the streamer object
        context_.reset();
        return false;
    }


    google::cloud::speech::v1::StreamingRecognizeRequest config_request;
    auto* streaming_config = config_request.mutable_streaming_config();
    streaming_config->mutable_config()->set_language_code(language_code);
    streaming_config->mutable_config()->set_sample_rate_hertz(sample_rate_hz);
    streaming_config->mutable_config()->set_encoding(google::cloud::speech::v1::RecognitionConfig::LINEAR16);
    streaming_config->set_interim_results(interim_results);
    // Add other RecognitionConfig settings as needed, e.g., model, punctuation
    // streaming_config->mutable_config()->set_enable_automatic_punctuation(true);
    // streaming_config->mutable_config()->set_model("telephony");

    if (!streamer_->Write(config_request, grpc::WriteOptions()).get()) { // .get() blocks until write is complete
        fprintf(stderr, "GoogleSttStreamer::StartStream: Failed to send initial config request.\n");
        if (result_callback_c_) {
            result_callback_c_(nullptr, false, "Failed to send initial config to Google STT", user_data_c_);
        }
        // Attempt to close gracefully, though it might fail if stream is broken
        streamer_->WritesDone().get(); 
        streamer_->Finish().get(); 
        streamer_.reset();
        context_.reset();
        return false;
    }

    stream_active_ = true;
    reader_thread_active_ = true;
    reader_thread_ = std::thread(&GoogleSttStreamer::ReadLoop, this);

    return true;
}

void GoogleSttStreamer::ReadLoop() {
    while (reader_thread_active_.load()) {
        if (!streamer_) {
             if (result_callback_c_) { // Should not happen if reader_thread_active_ is true
                result_callback_c_(nullptr, false, "Streamer object is null in ReadLoop", user_data_c_);
            }
            break;
        }
        
        auto read_future = streamer_->Read();
        if (!read_future.valid()) { // Check if the future is valid before calling get()
             if (result_callback_c_) {
                result_callback_c_(nullptr, false, "Read future is invalid", user_data_c_);
            }
            break; // Exit loop if future is invalid
        }

        auto response_variant = read_future.get(); // This blocks until a message is received or stream ends/errors

        if (!reader_thread_active_.load()) { // Check again after blocking read
            break;
        }

        if (absl::holds_alternative<google::cloud::speech::v1::StreamingRecognizeResponse>(response_variant)) {
            const auto& response = absl::get<google::cloud::speech::v1::StreamingRecognizeResponse>(response_variant);
            if (response.has_error()) {
                if (result_callback_c_) {
                    result_callback_c_(nullptr, false, response.error().message().c_str(), user_data_c_);
                }
                // Potentially stop the loop on error, depending on severity.
                // For now, continue reading unless reader_thread_active_ becomes false.
                // If the error is fatal, the stream might close itself.
            } else if (response.results_size() > 0) {
                const auto& result = response.results(0); // Assuming one result per response for simplicity
                bool is_final = result.is_final();
                if (result.alternatives_size() > 0) {
                    const auto& alternative = result.alternatives(0);
                    if (result_callback_c_) {
                        result_callback_c_(alternative.transcript().c_str(), is_final, nullptr, user_data_c_);
                    }
                } else if (is_final) { // Handle case where is_final is true but no alternatives (e.g. empty audio)
                     if (result_callback_c_) {
                        result_callback_c_("", is_final, nullptr, user_data_c_);
                    }
                }
                // If it's a final result and we are not doing continuous recognition,
                // we might want to stop the reader_thread_active_ here,
                // but that logic is better handled by CloseStream or FinishStream from the C plugin side.
            }
        } else if (absl::holds_alternative<google::cloud::Status>(response_variant)) {
            // Stream has ended (either normally or due to an error)
            const auto& status = absl::get<google::cloud::Status>(response_variant);
            if (!status.ok() && result_callback_c_) {
                 // Filter out CANCELLED which might be normal if CloseStream was called
                if (status.code() != google::cloud::StatusCode::kCancelled) {
                    result_callback_c_(nullptr, false, status.message().c_str(), user_data_c_);
                }
            }
            break; // Exit loop as stream is finished
        } else {
            // Should not happen (holds_alternative<StatusOr<optional<T>>> case)
             if (result_callback_c_) {
                result_callback_c_(nullptr, false, "Unknown variant in response_variant", user_data_c_);
            }
            break;
        }
    }
    reader_thread_active_ = false; // Ensure it's marked as inactive when loop exits
}


bool GoogleSttStreamer::SendAudio(const void* buffer, int size) {
    if (!stream_active_.load() || !streamer_) {
        // fprintf(stderr, "GoogleSttStreamer::SendAudio: Stream not active or streamer not initialized.\n");
        // This can be noisy if called when stream is legitimately closing.
        // The C plugin should not call this if stream_active is false.
        return false;
    }

    google::cloud::speech::v1::StreamingRecognizeRequest request;
    request.set_audio_content(buffer, size);
    
    // Write is asynchronous. We use .get() to make it behave synchronously for this call.
    // In a high-performance scenario, one might manage these futures differently.
    auto write_status = streamer_->Write(request, grpc::WriteOptions()).get();
    if (!write_status) {
         fprintf(stderr, "GoogleSttStreamer::SendAudio: Failed to write audio to stream.\n");
         if (result_callback_c_) {
            // This error should ideally be propagated.
            // However, calling callback from here might be tricky with threading.
            // Best if the ReadLoop detects the stream error caused by write failure.
            // For now, just log and return false. The ReadLoop should eventually pick up a stream error.
         }
        return false;
    }
    return true;
}

bool GoogleSttStreamer::FinishStream() { // Corresponds to WritesDone
    if (!stream_active_.load() || !streamer_) {
        fprintf(stderr, "GoogleSttStreamer::FinishStream: Stream not active or streamer not initialized.\n");
        return false;
    }
    
    // Signal that no more messages will be sent on the client-to-server stream.
    // .get() makes it synchronous for this call.
    auto writes_done_status = streamer_->WritesDone().get();
    if (!writes_done_status) {
        fprintf(stderr, "GoogleSttStreamer::FinishStream: Failed to call WritesDone on stream.\n");
        // Error might be reported by ReadLoop as well.
        return false;
    }
    // Do not set stream_active_ to false here. The stream is still open for reading results.
    // The ReadLoop will continue until the server closes the stream from its end after processing all audio.
    return true;
}

void GoogleSttStreamer::CloseStream() {
    std::unique_lock<std::mutex> lock(stream_mutex_);

    if (reader_thread_active_.load()) {
        reader_thread_active_ = false; // Signal reader thread to stop
        // If streamer_ exists, try to cancel operations to unblock ReadLoop quickly
        if (context_) {
            context_->TryCancel();
        }
    }

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    
    // After reader thread is joined, finalize the gRPC stream
    if (streamer_) {
        // We expect WritesDone to have been called already by FinishStream() if it was a clean shutdown.
        // Calling it again might be benign or error if already called.
        // streamer_->WritesDone().get(); // Optional: ensure it's done.

        auto status = streamer_->Finish().get(); // This is crucial to properly close the gRPC RPC.
        if (!status.ok()) {
            fprintf(stderr, "GoogleSttStreamer::CloseStream: Stream finish error: %s\n", status.message().c_str());
            if (result_callback_c_ && status.code() != google::cloud::StatusCode::kCancelled) { // Avoid double reporting if already cancelled
                 // It's possible the callback was already invoked by ReadLoop if the stream failed.
                 // This is a last resort notification.
                // result_callback_c_(nullptr, false, status.message().c_str(), user_data_c_);
            }
        }
        streamer_.reset(); // Release the unique_ptr
    }

    if (context_) {
        context_.reset(); // Release the context
    }
    stream_active_ = false; // Mark stream as fully inactive
}


// --- C Wrapper Implementation ---

extern "C" {

google_stt_streamer_handle_t google_stt_streamer_create(const char* credentials_path, google_stt_result_callback_t callback, void* user_data) {
    try {
        std::string cred_path_str = credentials_path ? credentials_path : "";
        GoogleSttStreamer* streamer = new GoogleSttStreamer(cred_path_str, callback, user_data);
        if (streamer && streamer->speech_client_ == nullptr) { // Check if client initialization failed
            delete streamer;
            return nullptr;
        }
        return static_cast<google_stt_streamer_handle_t>(streamer);
    } catch (const std::exception& e) {
        fprintf(stderr, "Exception in google_stt_streamer_create: %s\n", e.what());
        return nullptr;
    } catch (...) {
        fprintf(stderr, "Unknown exception in google_stt_streamer_create\n");
        return nullptr;
    }
}

void google_stt_streamer_destroy(google_stt_streamer_handle_t handle) {
    if (handle) {
        GoogleSttStreamer* streamer = static_cast<GoogleSttStreamer*>(handle);
        delete streamer;
    }
}

int google_stt_streamer_start(google_stt_streamer_handle_t handle, const char* language_code, int sample_rate_hz, int interim_results) {
    if (!handle) return -1; // Invalid handle
    GoogleSttStreamer* streamer = static_cast<GoogleSttStreamer*>(handle);
    if (!streamer->speech_client_) return -2; // Client not initialized
    
    std::string lang_str = language_code ? language_code : "en-US"; // Default if null
    return streamer->StartStream(lang_str, sample_rate_hz, interim_results != 0) ? 0 : -1;
}

int google_stt_streamer_send_audio(google_stt_streamer_handle_t handle, const void* audio_buffer, int buffer_size) {
    if (!handle) return -1;
    GoogleSttStreamer* streamer = static_cast<GoogleSttStreamer*>(handle);
     if (!streamer->speech_client_ || !streamer->stream_active_.load()) return -2; // Client not init or stream not active

    return streamer->SendAudio(audio_buffer, buffer_size) ? 0 : -1;
}

int google_stt_streamer_finish(google_stt_streamer_handle_t handle) { // WritesDone
    if (!handle) return -1;
    GoogleSttStreamer* streamer = static_cast<GoogleSttStreamer*>(handle);
    if (!streamer->speech_client_ || !streamer->stream_active_.load()) return -2;

    return streamer->FinishStream() ? 0 : -1;
}

void google_stt_streamer_close(google_stt_streamer_handle_t handle) {
    if (!handle) return;
    GoogleSttStreamer* streamer = static_cast<GoogleSttStreamer*>(handle);
    // No need to check speech_client_ here, CloseStream handles internal checks
    streamer->CloseStream();
}

} // extern "C"
