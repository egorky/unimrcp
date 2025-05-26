# UniMRCP Plugin para Google Cloud Speech-to-Text (`google_srg_plugin`)

## 1. Visión General

El `google_srg_plugin` es un motor de reconocimiento de voz para la plataforma UniMRCP. Permite a las aplicaciones que utilizan UniMRCP (como Asterisk con `asterisk-unimrcp`) emplear los potentes servicios de Google Cloud Speech-to-Text (STT) para convertir audio en texto.

Este plugin interactúa con la API de Google Cloud STT utilizando las bibliotecas cliente oficiales de Google Cloud C++ a través de gRPC, lo que permite un streaming de audio eficiente y reconocimiento en tiempo real.

## 2. Características Principales

*   **Acceso a los Modelos de Reconocimiento de Google:** Utiliza los modelos de reconocimiento de voz de última generación de Google, incluyendo modelos específicos para telefonía, comandos, dictado médico, etc.
*   **Amplio Soporte de Idiomas:** Soporta todos los idiomas y dialectos disponibles en Google Cloud STT.
*   **Streaming en Tiempo Real:** Envía audio de forma continua a Google STT y puede recibir transcripciones parciales (resultados intermedios).
*   **Resultados Intermedios:** Capacidad de recibir transcripciones provisionales a medida que el audio es procesado, mejorando la interactividad en aplicaciones en tiempo real.
*   **Puntuación Automática:** Soporte para la puntuación automática gestionada por Google STT (si está habilitada y es compatible con el modelo/idioma).
*   **Configuración Flexible:** Permite la configuración de parámetros como el idioma por defecto, la ruta a las credenciales, y la habilitación de características a través del archivo `unimrcpserver.xml`.
*   **Adaptación de Voz (Contexto):** Aunque no se detalla en la configuración básica, la API de Google STT permite proporcionar contexto para mejorar la precisión (ej. frases comunes). Esto podría ser una futura extensión.

## 3. Prerrequisitos

### 3.1. UniMRCP
*   **UniMRCP Server:** Versión 1.7.0 o superior (o la versión con la que se esté desarrollando/probando).

### 3.2. Dependencias de Google Cloud
*   **Bibliotecas Cliente de Google Cloud C++ (Speech):** Necesitarás compilar e instalar las bibliotecas `google-cloud-cpp`, específicamente el componente `speech`.
    *   **Obtención:** Sigue las instrucciones oficiales en [Google Cloud C++ SDK GitHub](https://github.com/googleapis/google-cloud-cpp) (consulta el archivo `INSTALL.md`).
    *   **Versión:** Se recomienda usar una versión reciente y estable.
*   **gRPC:** Es una dependencia de `google-cloud-cpp`.
    *   **Obtención:** Generalmente se compila como parte de `google-cloud-cpp` o se puede instalar por separado desde [gRPC GitHub](https://github.com/grpc/grpc).
    *   **Versión:** La versión compatible con la biblioteca cliente de Google Cloud C++ seleccionada.
*   **Protocol Buffers (Protobuf):** Requerido por gRPC y `google-cloud-cpp`.
    *   **Obtención:** Similar a gRPC, a menudo se gestiona a través de la compilación de `google-cloud-cpp` o desde [Protobuf GitHub](https://github.com/protocolbuffers/protobuf).
    *   **Versión:** La versión compatible.

**Instalación de Dependencias de Google (Alto Nivel):**
La forma más común es clonar el repositorio de `google-cloud-cpp` y seguir sus instrucciones de compilación, las cuales a menudo incluyen la compilación de gRPC y Protobuf desde submódulos o directorios de terceros.
```bash
# Ejemplo conceptual (consulta la documentación oficial de google-cloud-cpp para detalles precisos)
git clone https://github.com/googleapis/google-cloud-cpp.git
cd google-cloud-cpp
# Sigue las instrucciones para instalar prerrequisitos (cmake, compilador C++, etc.)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGOOGLE_CLOUD_CPP_ENABLE_speech=ON # Habilita solo speech
cmake --build build --target install
```
Asegúrate de que estas bibliotecas estén instaladas en una ubicación donde el sistema de compilación de UniMRCP pueda encontrarlas (ej. `/usr/local`) o configura `CMAKE_PREFIX_PATH` o `PKG_CONFIG_PATH` adecuadamente durante la compilación de UniMRCP.

### 3.3. Configuración del Proyecto Google Cloud
1.  **Proyecto Google Cloud Activo:** Necesitas una cuenta de Google Cloud y un proyecto activo.
2.  **Habilitar la API "Cloud Speech-to-Text":**
    *   Ve a la [Consola de Google Cloud](https://console.cloud.google.com/).
    *   Selecciona tu proyecto.
    *   Ve a "APIs y Servicios" > "Biblioteca".
    *   Busca "Cloud Speech-to-Text API" y habilítala.
3.  **Crear una Cuenta de Servicio y Credenciales JSON:**
    *   Ve a "IAM y Administración" > "Cuentas de servicio".
    *   Haz clic en "CREAR CUENTA DE SERVICIO".
    *   Dale un nombre (ej., `unimrcp-stt-user`) y una descripción.
    *   **Asigna Roles:** Dale el rol "Usuario de API de Cloud Speech" (o "Cloud Speech Service Agent" o uno más específico que permita `speech.recognize`).
    *   Haz clic en "LISTO".
    *   Busca la cuenta de servicio recién creada, haz clic en los tres puntos (Acciones) y selecciona "Administrar claves".
    *   Haz clic en "AGREGAR CLAVE" > "Crear nueva clave".
    *   Selecciona "JSON" como tipo de clave y haz clic en "CREAR".
    *   Se descargará un archivo JSON. **Guarda este archivo de forma segura**, ya que contiene tus credenciales. Lo necesitarás para la configuración del plugin.

## 4. Compilación e Instalación

### 4.1. Dependencias de Compilación
*   Un compilador C y C++ compatible (GCC, Clang).
*   CMake (versión 3.10 o superior) o Autotools (autoconf, automake, libtool).
*   `pkg-config` (especialmente para Autotools).
*   Las bibliotecas de desarrollo para `google-cloud-cpp-speech`, `grpc++`, y `protobuf` deben estar instaladas y detectables.

### 4.2. Obtener el Plugin
El `google_srg_plugin` es parte del árbol de fuentes de UniMRCP. Clona el repositorio de UniMRCP si aún no lo has hecho.

### 4.3. Integración con el Sistema de Compilación de UniMRCP

Para que el plugin `google_srg_plugin` se compile junto con UniMRCP, es necesario integrarlo en el sistema de compilación principal.

#### 4.3.1. Usando Autotools:

1.  **`configure.ac` (Raíz de UniMRCP):**
    Asegúrate de que el script `configure.ac` principal de UniMRCP esté configurado para:
    *   **Detectar el compilador C++:**
        ```autoconf
        AC_PROG_CXX
        ```
    *   **Buscar las dependencias usando `pkg-config`:**
        ```autoconf
        PKG_CHECK_MODULES([GOOGLE_CLOUD_CPP_SPEECH], [google-cloud-cpp-speech >= 1.23.0], [], [AC_MSG_ERROR([google-cloud-cpp-speech library not found. Searched version >= 1.23.0])])
        PKG_CHECK_MODULES([GRPC], [grpc++ >= 1.30.0], [], [AC_MSG_ERROR([gRPC library not found. Searched version >= 1.30.0])])
        PKG_CHECK_MODULES([PROTOBUF], [protobuf >= 3.12.0], [], [AC_MSG_ERROR([Protocol Buffers library not found. Searched version >= 3.12.0])])

        AC_SUBST(GOOGLE_CLOUD_CPP_SPEECH_CFLAGS)
        AC_SUBST(GOOGLE_CLOUD_CPP_SPEECH_LIBS)
        AC_SUBST(GRPC_CFLAGS)
        AC_SUBST(GRPC_LIBS)
        AC_SUBST(PROTOBUF_CFLAGS)
        AC_SUBST(PROTOBUF_LIBS)
        ```
        *Nota: Ajusta las versiones mínimas según sea necesario.*
    *   **Opcional: Verificar estándar C++ (ej. C++17):** Si `google-cloud-cpp` lo requiere.
        ```autoconf
        # AX_CXX_COMPILE_STDCXX(17, [noext], [mandatory]) # Descomentar si se usa esta macro de autoconf-archive
        ```
    *   **Añadir el Makefile del plugin a `AC_CONFIG_FILES`:**
        ```autoconf
        AC_CONFIG_FILES([
            # ... otros Makefiles ...
            plugins/google_srg_plugin/Makefile
            # ...
        ])
        ```

2.  **`Makefile.am` Raíz (o `plugins/Makefile.am`):**
    Añade el directorio del plugin a la variable `SUBDIRS` (o una específica para plugins si UniMRCP la usa):
    ```automake
    SUBDIRS = \
        # ... otros directorios ...
        plugins/google_srg_plugin \
        # ...
    ```

#### 4.3.2. Usando CMake:

1.  **`CMakeLists.txt` Raíz (UniMRCP):**
    Añade el subdirectorio del plugin:
    ```cmake
    # ... otros plugins ...
    add_subdirectory(plugins/google_srg_plugin)
    ```
2.  **Notas sobre `CMAKE_PREFIX_PATH`:**
    Si las dependencias (gRPC, Protobuf, Google Cloud C++ SDK) están instaladas en ubicaciones no estándar, los usuarios necesitarán establecer la variable de entorno `CMAKE_PREFIX_PATH` al ejecutar `cmake` para que `find_package()` pueda localizarlas.
    ```bash
    export CMAKE_PREFIX_PATH="/ruta/a/instalacion/grpc:/ruta/a/instalacion/protobuf:/ruta/a/instalacion/google-cloud-cpp"
    # o
    cmake -DCMAKE_PREFIX_PATH="/ruta/a/instalacion/grpc;/ruta/a/instalacion/protobuf;/ruta/a/instalacion/google-cloud-cpp" ..
    ```

### 4.4. Comandos de Compilación

Una vez que el sistema de compilación de UniMRCP está configurado para incluir el plugin:

*   **Autotools:**
    ```bash
    cd /ruta/a/unimrcp
    ./bootstrap  # Si es una clonación fresca o configure.ac ha cambiado
    ./configure  # Asegúrate de que detecta las dependencias de Google
    make
    sudo make install
    ```
*   **CMake:**
    ```bash
    cd /ruta/a/unimrcp
    mkdir build && cd build
    cmake ..   # Pasa -DCMAKE_PREFIX_PATH si es necesario
    make
    sudo make install
    ```
Tras la instalación, el archivo `googlesrg.so` (o similar) debería estar en el directorio de plugins de UniMRCP (ej. `/usr/local/unimrcp/plugins`).

## 5. Configuración del Servidor UniMRCP (`unimrcpserver.xml`)

Para usar el plugin `google_srg_plugin`, necesitas configurarlo en `unimrcpserver.xml`.

### 5.1. Definición del Motor del Plugin

Dentro de la sección `<resourcemap>` (o `<engine-map>` si es una configuración más antigua), define el motor:

```xml
<engine name="GoogleSRG1" so-name="googlesrg" enable="true">
    <!-- Ruta al archivo JSON de credenciales de la cuenta de servicio de Google Cloud -->
    <param name="google-credentials-path" value="/opt/unimrcp/config/your-gcp-project-credentials.json"/>
    
    <!-- Código de idioma BCP-47 por defecto (ej. "en-US", "es-ES", "fr-FR") -->
    <param name="google-default-language" value="en-US"/>
    
    <!-- Habilitar/deshabilitar resultados intermedios por defecto (opcional, default: "false") -->
    <!-- Los resultados intermedios son transcripciones provisionales -->
    <param name="google-default-interim-results" value="false"/>
    
    <!-- Modelo de reconocimiento específico de Google STT (opcional, default: modelo estándar de Google) -->
    <!-- Ejemplos: "telephony", "latest_long", "medical_dictation" -->
    <param name="google-default-model" value=""/> 
    
    <!-- Habilitar/deshabilitar puntuación automática (opcional, default: "false") -->
    <param name="google-enable-automatic-punctuation" value="false"/>
    
    <!-- Controlar el uso del VAD de Google (opcional, default: "true") -->
    <!-- "true": Confiar principalmente en el VAD de Google para la detección de fin de habla. -->
    <!-- "false": Confiar más en el VAD de MPF de UniMRCP. -->
    <param name="google-vad-enable" value="true"/>
</engine>
```

**Parámetros:**
*   `google-credentials-path` (string, **obligatorio**): Ruta absoluta al archivo JSON de credenciales de la cuenta de servicio de Google Cloud. Si esta ruta está vacía o no se especifica, el plugin intentará usar las Credenciales por Defecto de la Aplicación de Google (útil si se ejecuta en GCP o con la variable `GOOGLE_APPLICATION_CREDENTIALS` configurada).
*   `google-default-language` (string, **obligatorio**): Código de idioma BCP-47 por defecto si el cliente MRCP no especifica uno. Ejemplos: `en-US`, `es-ES`, `ja-JP`.
*   `google-default-interim-results` (string: "true" o "false", opcional, default: "false"): Controla si se solicitan resultados intermedios a Google STT.
*   `google-default-model` (string, opcional, default: ""): Especifica un [modelo de reconocimiento](https://cloud.google.com/speech-to-text/docs/speech-to-text-supported-models) de Google STT. Si está vacío, se usa el modelo estándar de Google para el idioma.
*   `google-enable-automatic-punctuation` (string: "true" o "false", opcional, default: "false"): Habilita la puntuación automática si el modelo y el idioma lo soportan.
*   `google-vad-enable` (string: "true" o "false", opcional, default: "true"): Si es "true", el plugin confía principalmente en la detección de fin de habla de Google. Si es "false", el VAD (Voice Activity Detector) de MPF de UniMRCP tendrá un rol más activo, y el plugin podría finalizar el envío de audio a Google basándose en los eventos de MPF.

### 5.2. Asociación con un Perfil MRCP

Para que los clientes MRCP puedan usar este motor, debes asociarlo con un perfil MRCP (generalmente MRCPv2) dentro de la sección `<profile-map>`:

```xml
<profile name="GoogleSTT-MRCPv2-Profile" version="2" enable="true">
    <resourcemap>
        <!-- Asocia el recurso 'speechrecog' con el motor 'GoogleSRG1' definido arriba -->
        <resource name="speechrecog" engine="GoogleSRG1" enable="true"/>
        <!-- Otros recursos como speechsynth podrían definirse aquí -->
    </resourcemap>
    <!-- Configuración de transporte, ej. SIP y RTP/RTCP -->
    <rtp type="rtp-legacy" enable="true">
        <settings>
            <param name="rtp-ip" value="auto"/> <!-- O IP específica del servidor UniMRCP -->
            <param name="rtp-port-min" value="4000"/>
            <param name="rtp-port-max" value="5000"/>
            <!-- Otros parámetros RTP/RTCP -->
        </settings>
    </rtp>
    <sip type="sip-uac" enable="true">
        <settings>
            <param name="server-ip" value="auto"/> <!-- IP del servidor UniMRCP -->
            <param name="server-port" value="8060"/>
            <param name="force-destination" value="false"/>
            <!-- Otros parámetros SIP -->
        </settings>
    </sip>
    <!-- También puedes configurar MRCPv1 si es necesario -->
</profile>
```
El `name` del perfil (ej., "GoogleSTT-MRCPv2-Profile") es importante, ya que será usado por los clientes MRCP (como Asterisk) para seleccionar esta configuración.

## 6. Configuración de Asterisk (con `asterisk-unimrcp` / `res_speech_unimrcp`)

Para utilizar el plugin `google_srg_plugin` desde Asterisk, necesitarás el módulo `asterisk-unimrcp` (que proporciona `res_speech_unimrcp.so`) instalado y configurado en tu sistema Asterisk. Este módulo actúa como un conector entre la API de Reconocimiento de Voz Genérica de Asterisk (Generic Speech API) y el servidor UniMRCP.

### 6.1. Prerrequisito: `asterisk-unimrcp`
Asegúrate de tener `asterisk-unimrcp` compilado e instalado en tu sistema Asterisk. Puedes obtenerlo desde el [repositorio oficial de `asterisk-unimrcp`](https://github.com/unispeech/asterisk-unimrcp) o desde el gestor de paquetes de tu distribución si está disponible.

### 6.2. Configuración de `unimrcp.conf` (Asterisk)
Este archivo, usualmente ubicado en `/etc/asterisk/unimrcp.conf`, configura cómo Asterisk (específicamente `res_speech_unimrcp.so`) se comunica con el servidor UniMRCP.

Define un perfil de cliente que apunte a tu servidor UniMRCP y especifique el perfil del servidor UniMRCP que utiliza el motor `googlesrg`.

```ini
; unimrcp.conf (en el directorio de configuración de Asterisk)

[general]
; Nombre del perfil de servidor UniMRCP por defecto a usar.
; Este nombre DEBE coincidir con un perfil definido abajo.
default-server-profile = MyUniMRCPServerProfile

; Nivel de log para el conector UniMRCP en Asterisk (opcional)
; log-level = NOTICE ; O DEBUG, WARNING, ERROR

; Versión de MRCP a usar por defecto (1 o 2)
; mrcp-version = 2

[MyUniMRCPServerProfile] ; Nombre de este perfil de cliente UniMRCP
; Dirección IP del servidor UniMRCP
server-ip = 127.0.0.1 ; O la IP donde corre tu servidor UniMRCP
; Puerto RTSP del servidor UniMRCP (usualmente 8060)
server-port = 8060

; IMPORTANTE: Nombre del perfil MRCPv2 (o MRCPv1) definido en unimrcpserver.xml
; Este perfil en unimrcpserver.xml debe estar configurado para usar el motor "GoogleSRG1"
default-recognizer-profile = GoogleSTT-MRCPv2-Profile 
; default-synthesizer-profile = MySynthProfile ; Si también usas TTS

; Opcional: otros parámetros como timeouts, configuración SIP específica si usas SIP en lugar de RTSP.
; rtp-port-min = 10000
; rtp-port-max = 10100
```

**Puntos Clave:**
*   **`default-server-profile`**: Especifica el nombre del perfil de cliente (`[MyUniMRCPServerProfile]`) a usar.
*   **`server-ip` / `server-port`**: Deben apuntar a tu servidor UniMRCP en ejecución.
*   **`default-recognizer-profile`**: Es crucial. Debe coincidir **exactamente** con el `name` del perfil (ej. "GoogleSTT-MRCPv2-Profile") que definiste en `unimrcpserver.xml` (ver Sección 5.2). Este perfil en `unimrcpserver.xml` es el que está configurado para usar el motor `GoogleSRG1` (que a su vez usa `googlesrg.so`).

### 6.3. Configuración de `speech.conf` (Asterisk)
Este archivo, usualmente en `/etc/asterisk/speech.conf`, configura el motor de reconocimiento de voz por defecto para la API Genérica de Voz de Asterisk.

```ini
; speech.conf (en el directorio de configuración de Asterisk)

[general]
; Establece UniMRCP como el motor de reconocimiento de voz por defecto.
; El valor debe ser "unimrcp:<nombre-del-perfil-en-unimrcp.conf>"
default_speech_recognition_engine = unimrcp:MyUniMRCPServerProfile
; default_speech_synthesis_engine = unimrcp:MyUniMRCPServerProfile ; Si también usas TTS
```

**Puntos Clave:**
*   `default_speech_recognition_engine`: Debe ser `unimrcp:` seguido del nombre del perfil que definiste en `unimrcp.conf` (ej. `MyUniMRCPServerProfile`).

### 6.4. Ejemplo de Dialplan de Asterisk (`extensions.conf`)
Una vez configurados `unimrcp.conf` y `speech.conf`, puedes usar las aplicaciones de la API Genérica de Voz en tu dialplan:

```dialplan
exten => google_stt_test,1,NoOp(Iniciando prueba de reconocimiento con Google STT vía UniMRCP)
 same => n,Answer()
 same => n,Wait(1)

 ; Iniciar la sesión de reconocimiento de voz.
 ; Si default_speech_recognition_engine está configurado en speech.conf,
 ; no necesitas especificar el motor aquí.
 ; Alternativamente, puedes especificarlo: SpeechCreate(unimrcp:MyUniMRCPServerProfile)
 same => n,SpeechCreate() 
 same => n,NoOp(Estado de SpeechCreate: ${SPEECH_STATUS(0)})
 same => n,GotoIf($["${SPEECH_STATUS(0)}" != "OK"]?speech_error)

 ; Opcional: Establecer el idioma para esta sesión de reconocimiento.
 ; Esto se enviará como la cabecera Speech-Language en MRCP.
 same => n,Set(SPEECH_LANGUAGE()=es-ES) ; Ejemplo para español
 ; same => n,Set(SPEECH_LANGUAGE()=en-GB) ; Ejemplo para inglés británico

 ; Opcional: Intentar pasar parámetros específicos del proveedor.
 ; La forma exacta puede depender de la versión de asterisk-unimrcp.
 ; Esto podría enviarse como cabeceras MRCP.
 ; same => n,Set(SPEECH_PARAMS(X-Google-Model)=telephony)
 ; same => n,Set(SPEECH_PARAMS(X-Google-Interim-Results)=true)

 ; Iniciar el reconocimiento.
 ; SpeechRecognize(prompt_a_reproducir, timeout_en_ms, opciones)
 ; El audio de la llamada actual se enviará al motor UniMRCP.
 ; Si se especifica un prompt, se reproducirá y el reconocimiento comenzará después.
 ; 'b' en opciones habilita barge-in.
 same => n,SpeechRecognize(Por favor, diga algo después del tono.,5000,b)
 same => n,NoOp(Estado de SpeechRecognize: ${SPEECH_STATUS(0)})
 same => n,NoOp(Razón del estado: ${SPEECH_STATUS_REASON(0)})
 same => n,GotoIf($["${SPEECH_STATUS(0)}" != "OK"]?speech_error)

 same => n,NoOp(Texto Reconocido: ${SPEECH_TEXT(0)})
 same => n,NoOp(Confianza: ${SPEECH_CONFIDENCE(0)})
 same => n,NoOp(Gramática/Input (puede ser NLSML): ${SPEECH_GRAMMAR(0)})

 ; Destruir el recurso de reconocimiento de voz
 same => n,SpeechDestroy()
 same => n,Hangup()

same => n(speech_error),NoOp(Error durante la operación de reconocimiento de voz: ${SPEECH_STATUS(0)} - ${SPEECH_STATUS_REASON(0)})
 same => n,SpeechDestroy() ; Intenta limpiar incluso en caso de error
 same => n,Hangup()
```

**Notas sobre el Dialplan:**
*   `SpeechCreate()`: Inicia una sesión con el motor de voz. Si has configurado `default_speech_recognition_engine` en `speech.conf`, no necesitas pasar argumentos.
*   `Set(SPEECH_LANGUAGE()=...)`: Te permite cambiar el idioma por solicitud. El plugin `google_srg_plugin` usará este valor si está presente.
*   `Set(SPEECH_PARAMS(HeaderName)=valor)`: Podría usarse para enviar cabeceras MRCP específicas del proveedor si `asterisk-unimrcp` lo soporta adecuadamente para RECOGNIZE.
*   `SpeechRecognize()`: Envía el audio al motor UniMRCP.
*   Variables de Canal: `SPEECH_TEXT(0)`, `SPEECH_CONFIDENCE(0)`, `SPEECH_GRAMMAR(0)`, `SPEECH_STATUS(0)` y `SPEECH_STATUS_REASON(0)` se llenan con los resultados.

Asegúrate de recargar la configuración de Asterisk (`core reload` o `dialplan reload`, `reload res_speech_unimrcp.so` si es necesario) después de realizar cambios en estos archivos.

## 7. Uso Avanzado

*   **Cambio de Idioma en Tiempo de Ejecución:**
    Como se muestra en el dialplan, usa `Set(SPEECH_LANGUAGE()=xx-XX)` antes de `SpeechRecognize()`.

*   **Resultados Intermedios y Selección de Modelo por Solicitud:**
    La capacidad de enviar cabeceras MRCP personalizadas como `X-Google-Interim-Results` o `X-Google-Model` usando `Set(SPEECH_PARAMS(HeaderName)=value)` depende de la versión y las capacidades de `asterisk-unimrcp`. Consulta su documentación. Si `asterisk-unimrcp` no lo soporta directamente, estas configuraciones se basarán en los valores por defecto del servidor UniMRCP.

## 8. Troubleshooting

*   **Fallas de Autenticación:**
    *   Verifica que la ruta en `google-credentials-path` sea correcta y el archivo JSON sea legible por el usuario que ejecuta UniMRCP.
    *   Asegúrate de que la cuenta de servicio tenga los permisos IAM correctos en GCP (rol "Usuario de API de Cloud Speech" o similar).
    *   El reloj del servidor UniMRCP debe estar sincronizado (NTP), ya que la autenticación es sensible al tiempo.
*   **Problemas de Red:**
    *   Asegúrate de que el servidor UniMRCP pueda alcanzar `speech.googleapis.com` en el puerto 443. Verifica firewalls y rutas de red.
    *   Problemas de DNS pueden impedir la resolución de los endpoints de Google.
*   **Errores de "Library not found" o "Plugin load failed":**
    *   Las bibliotecas de `google-cloud-cpp`, `grpc`, o `protobuf` no están en la ruta de enlace del sistema (`ldconfig -p | grep libgoogle_cloud_cpp_speech`).
    *   Ejecuta `ldd /ruta/a/plugin/googlesrg.so` para verificar si todas las dependencias se resuelven.
    *   Asegúrate de que las versiones compiladas sean compatibles.
*   **No se Reciben Resultados / Errores en el Reconocimiento:**
    *   Verifica los logs de UniMRCP (configurados en `logger.xml`) para mensajes del `GOOGLE-SRG_PLUGIN`.
    *   Verifica los logs de Asterisk (`/var/log/asterisk/full` o la CLI de Asterisk).
    *   Revisa la [Consola de Google Cloud](https://console.cloud.google.com/) bajo "Speech-to-Text API" > "Métricas" o "Registros" para errores o problemas de cuota reportados por Google.
    *   Asegúrate de que el formato de audio y la tasa de muestreo sean compatibles (el plugin usa LPCM a 8kHz o 16kHz por defecto).
*   **Logs Importantes:**
    *   **UniMRCP Server Log:** Usualmente definido en `logger.xml` o por argumentos de línea de comandos al iniciar `unimrcpserver`. Busca mensajes con `[GOOGLE-SRG-PLUGIN]`.
    *   **Asterisk CLI / Logs:** Proporciona información sobre el estado de `chan_unimrcp` y las aplicaciones `Speech*`.

## 9. Licencia

Este plugin se distribuye bajo la licencia Apache 2.0. Consulta el archivo `LICENSE` en el repositorio de UniMRCP para más detalles.

```
Copyright 2023-2024 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
