#ifndef GOOGLE_STT_STREAMER_HPP
#define GOOGLE_STT_STREAMER_HPP

#include "google/cloud/speech/v1/speech_client.h"
#include "google/cloud/grpc_options.h"
#include <grpcpp/grpcpp.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional> // For std::function if needed, though C callback is primary

// Forward declaration for the C++ class
class GoogleSttStreamer;

// C wrapper types
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GOOGLE_STT_LOG_LEVEL_ERROR,
    GOOGLE_STT_LOG_LEVEL_WARNING,
    GOOGLE_STT_LOG_LEVEL_INFO,
    GOOGLE_STT_LOG_LEVEL_DEBUG
} google_stt_log_level_e;

typedef void* google_stt_streamer_handle_t;
typedef void (*google_stt_result_callback_t)(const char* transcript, bool is_final, const char* error_msg, void* user_data);
typedef void (*google_stt_log_callback_t)(
    google_stt_log_level_e level, 
    const char* file, 
    int line, 
    const char* message, 
    void* user_data
);

google_stt_streamer_handle_t google_stt_streamer_create(
    const char* credentials_path, 
    google_stt_result_callback_t result_callback, 
    google_stt_log_callback_t log_callback, // New parameter
    void* user_data 
);
void google_stt_streamer_destroy(google_stt_streamer_handle_t handle);
google_stt_streamer_handle_t google_stt_streamer_start(
    google_stt_streamer_handle_t handle,
    const char* language_code,
    int sample_rate_hz,
    bool interim_results, // Changed from int to bool for consistency
    const char* model,
    bool enable_automatic_punctuation
);
int google_stt_streamer_send_audio(google_stt_streamer_handle_t handle, const void* audio_buffer, int buffer_size);
int google_stt_streamer_finish(google_stt_streamer_handle_t handle); // WritesDone
void google_stt_streamer_close(google_stt_streamer_handle_t handle); // Finish stream and stop read thread

#ifdef __cplusplus
} // extern "C"
#endif


// C++ Class Definition
class GoogleSttStreamer {
public:
    GoogleSttStreamer(
        const std::string& credentials_path, 
        google_stt_result_callback_t result_callback, 
        google_stt_log_callback_t log_callback, // New parameter
        void* user_data
    );
    ~GoogleSttStreamer();

    bool StartStream(
        const std::string& language_code,
        int sample_rate_hz,
        bool interim_results,
        const std::string& model,
        bool enable_automatic_punctuation
    );
    bool SendAudio(const void* buffer, int size);
    bool FinishStream(); // Corresponds to WritesDone
    void CloseStream();  // Closes stream, stops reader thread, calls Finish on gRPC stream

private:
    void ReadLoop();

    std::string credentials_path_;
    google_stt_result_callback_t result_callback_c_;
    google_stt_log_callback_t log_callback_c_;
    void* user_data_c_;

    std::unique_ptr<google::cloud::speech::v1::SpeechClient> speech_client_;
    std::shared_ptr<grpc::ClientContext> context_; // Recreate for each stream
    std::unique_ptr<google::cloud::AsyncStreamingReadWriteRpc<
        google::cloud::speech::v1::StreamingRecognizeRequest,
        google::cloud::speech::v1::StreamingRecognizeResponse>> streamer_;

    std::thread reader_thread_;
    std::atomic<bool> stream_active_;
    std::atomic<bool> reader_thread_active_;
    std::mutex stream_mutex_; // To protect access to streamer_ and context_ during critical operations
};

#endif // GOOGLE_STT_STREAMER_HPP
