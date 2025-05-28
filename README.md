# UniMRCP Plugin para Google Cloud Speech-to-Text (`google_srg_plugin`)

## 1. Visión General

El `google_srg_plugin` es un motor de reconocimiento de voz para la plataforma UniMRCP. Permite a las aplicaciones que utilizan UniMRCP (como Asterisk con `asterisk-unimrcp`) emplear los potentes servicios de Google Cloud Speech-to-Text (STT) para convertir audio en texto.

Este plugin interactúa con la API de Google Cloud STT utilizando las bibliotecas cliente oficiales de Google Cloud C++ a través de gRPC, lo que permite un streaming de audio eficiente y reconocimiento en tiempo real.

## 2. Características Principales

*   **Acceso a los Modelos de Reconocimiento de Google:** Utiliza los modelos de reconocimiento de voz de última generación de Google, incluyendo modelos específicos para telefonía, comandos, dictado médico, etc.
*   **Amplio Soporte de Idiomas:** Soporta todos los idiomas y dialectos disponibles en Google Cloud STT.
*   **Streaming en Tiempo Real:** Envía audio de forma continua a Google STT y puede recibir transcripciones parciales (resultados intermedios).
*   **Resultados Intermedios:** Capacidad de recibir transcripciones provisionales a medida que el audio es procesado, mejorando la interactividad en aplicacion es en tiempo real.
*   **Puntuación Automática:** Soporte para la puntuación automática gestionada por Google STT (si está habilitada y es compatible con el modelo/idioma).
*   **Configuración Flexible:** Permite la configuración de parámetros como el idioma por defecto, la ruta a las credenciales, y la habilitación de características a través del archivo `unimrcpserver.xml`.
*   **Adaptación de Voz (Contexto):** Aunque no se detalla en la configuración básica, la API de Google STT permite proporcionar contexto para mejorar la precisión (ej. frases comunes). Esto podría ser una futura extensión.

## 3. Mejoras Recientes

Este plugin ha sido actualizado para abordar lo siguiente:

*   **Propagación Correcta de Configuraciones de Reconocimiento:**
    *   Las configuraciones de `model` (ej., "telephony", "latest_long") y `enable_automatic_punctuation` establecidas en `unimrcpserver.xml` (o pasadas vía MRCP) ahora se propagan correctamente a la API de Google Cloud Speech-to-Text. Esto asegura que tus preferencias específicas de modelo de reconocimiento y configuraciones de puntuación se apliquen con precisión durante la transcripción.
*   **Registro Integrado de Logs:**
    *   El registro de errores y depuración del componente C++ subyacente (que maneja la comunicación gRPC con Google STT) ahora está integrado con el sistema de registro estándar de UniMRCP (`apt_log`). Esto significa que los logs de este plugin aparecerán de manera consistente con otros logs del servidor UniMRCP, respetando los niveles definidos en `logger.xml`, facilitando la solución de problemas.
*   **Cumplimiento de API y Precisión de Resultados:**
    *   El plugin cumple con los requisitos de la API Genérica de Voz de Asterisk.
    *   El texto de voz reconocido se formatea correctamente en el resultado NLSML para asegurar que pueble la variable de canal de Asterisk `${SPEECH_TEXT(0)}`.

Estos cambios mejoran la funcionalidad, configurabilidad y mantenibilidad del plugin.

## 4. Prerrequisitos

### 4.1. UniMRCP
*   **UniMRCP Server:** Versión 1.7.0 o superior (o la versión con la que se esté desarrollando/probando).

### 4.2. Configuración del Proyecto Google Cloud
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

### 4.3. Resumen de Dependencias de Google Cloud C++ SDK
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

## 5. Compilación e Instalación en Ubuntu 22.04 LTS

Esta sección te guía a través de la compilación del `google_srg_plugin` como parte de UniMRCP en un sistema Ubuntu 22.04 LTS. Este plugin se construye junto con el servidor UniMRCP.

### 5.1. Instalar Paquetes Prerrequisito

Primero, actualiza tu lista de paquetes e instala herramientas de compilación esenciales y bibliotecas:

```bash
sudo apt update
sudo apt install -y     build-essential     cmake     git     pkg-config     libapr1-dev     libaprutil1-dev     libsofia-sip-ua-dev     libsrtp2-dev     autoconf     automake     libtool
```

### 5.2. Instalar Google Cloud C++ SDK (con el componente Speech)

Este plugin requiere las bibliotecas cliente de Google Cloud C++, específicamente el componente Speech. La forma recomendada de instalarlas es compilándolas desde el código fuente, lo que asegura que obtengas versiones compatibles de gRPC y Protobuf.

1.  **Instalar dependencias adicionales para Google Cloud C++ SDK:**
    ```bash
    sudo apt install -y libcurl4-openssl-dev libssl-dev
    ```

2.  **Clonar el repositorio de Google Cloud C++ SDK:**
    ```bash
    git clone https://github.com/googleapis/google-cloud-cpp.git
    cd google-cloud-cpp
    ```

3.  **Configurar y Compilar (solo habilitar el servicio Speech para ahorrar tiempo):**
    El sistema de compilación de `google-cloud-cpp` descargará y compilará sus dependencias, incluyendo gRPC y Protobuf.
    ```bash
    cmake -S . -B cmake-out         -DCMAKE_BUILD_TYPE=Release         -DGOOGLE_CLOUD_CPP_ENABLE_speech=ON         -DCMAKE_INSTALL_PREFIX=/usr/local
    ```
    *   `-DCMAKE_INSTALL_PREFIX=/usr/local`: Instala las bibliotecas en `/usr/local`. Asegúrate de que esta ruta esté en las rutas de búsqueda de tu sistema para bibliotecas y `pkg-config`. Si eliges un prefijo diferente, necesitarás informar al sistema de compilación de UniMRCP.

4.  **Compilar e Instalar:**
    ```bash
    cmake --build cmake-out -- -j $(nproc)
    sudo cmake --install cmake-out
    ```
    Si usaste un `CMAKE_INSTALL_PREFIX` personalizado (ej., `/opt/google-cloud-cpp`), asegúrate de que tus variables de entorno estén configuradas para la compilación de UniMRCP:
    ```bash
    # Ejemplo:
    # export PKG_CONFIG_PATH=/opt/google-cloud-cpp/lib/pkgconfig:$PKG_CONFIG_PATH
    # export CMAKE_PREFIX_PATH=/opt/google-cloud-cpp:$CMAKE_PREFIX_PATH
    ```

### 5.3. Obtener el Código Fuente de UniMRCP

Si aún no lo has hecho, clona el repositorio de UniMRCP que contiene este plugin:
```bash
# Reemplaza con la URL correcta del repositorio UniMRCP que incluye este plugin
git clone https://github.com/unispeech/unimrcp.git # URL de ejemplo
cd unimrcp
```
*Nota: Asegúrate de que este árbol de fuentes de UniMRCP contenga el `google_srg_plugin` en su directorio `plugins`.*

### 5.4. Configurar y Compilar UniMRCP (con el Plugin Google SRG)

El `google_srg_plugin` tiene tanto `Makefile.am` (para Autotools) como `CMakeLists.txt` (para CMake). El método que uses depende de cómo compiles UniMRCP mismo.

#### 5.4.1. Opción 1: Usando Autotools

1.  **Bootstrap UniMRCP (si compilas desde un clon de git):**
    ```bash
    ./bootstrap
    ```

2.  **Configurar UniMRCP:**
    Ejecuta el script `configure`. Debería detectar automáticamente el compilador C++ y usar `pkg-config` para encontrar Google Cloud Speech, gRPC y Protobuf.
    ```bash
    ./configure --enable-cpp-plugins # Añade otras opciones de UniMRCP según necesites
    ```
    *   `--enable-cpp-plugins`: Esta opción (o una similar, revisa `./configure --help` de UniMRCP) podría ser necesaria para asegurar que los plugins basados en C++ se compilen.
    *   Asegúrate de que tu `configure.ac` de UniMRCP y archivos relacionados estén configurados para detectar C++ y las bibliotecas de Google Cloud vía `pkg-config` como se indica en el `README.md` original y el `Makefile.am` de este plugin.

3.  **Compilar e Instalar UniMRCP y el Plugin:**
    ```bash
    make -j $(nproc)
    sudo make install
    ```

#### 5.4.2. Opción 2: Usando CMake

1.  **Crear un directorio de compilación:**
    ```bash
    mkdir build && cd build
    ```

2.  **Configurar UniMRCP con CMake:**
    ```bash
    cmake .. # Añade opciones CMake de UniMRCP, ej., -DCMAKE_INSTALL_PREFIX=/opt/unimrcp
    ```
    El `CMakeLists.txt` principal de UniMRCP debería encontrar e incluir el subdirectorio `google_srg_plugin`.

3.  **Compilar e Instalar UniMRCP y el Plugin:**
    ```bash
    make -j $(nproc)
    sudo make install
    ```

### 5.5. Verificación
Después de la instalación, el plugin `googlesrg.so` debería estar presente en tu directorio de plugins de UniMRCP (ej., `/usr/local/unimrcp/plugins`).

Procede a configurar el plugin en `unimrcpserver.xml`.

## 6. Configuración del Servidor UniMRCP (`unimrcpserver.xml`)
(Full content from original README)

## 7. Configuración de Asterisk (con `asterisk-unimrcp` / `res_speech_unimrcp`)
(Full content from original README)

## 8. Uso Avanzado
(Full content from original README)

## 9. Solución de Problemas (Troubleshooting)
(Full content from original README)

## 10. Licencia
(Full content from original README)
```
