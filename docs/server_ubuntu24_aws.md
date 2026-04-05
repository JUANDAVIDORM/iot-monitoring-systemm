# Guia paso a paso: montar el servidor en AWS con Ubuntu 24.04

Esta guia asume que ya tienes el codigo del proyecto en GitHub (o al menos en tu PC) y que quieres **levantar solo el servidor** en una instancia EC2 con **Ubuntu Server 24.04 LTS**, usando **Docker**.

El objetivo final es:
- Instancia EC2 Ubuntu 24.04 corriendo Docker.
- Contenedor `iot-monitor-server` ejecutando `./server puerto archivoDeLogs`.
- Puertos abiertos para que clientes externos se conecten.

---

## 0. Requisitos previos

- Cuenta de AWS.
- Par de claves (.pem) para conectarte por SSH.
- Repositorio Git del proyecto (por ejemplo en GitHub).

---

## 1. Crear la instancia EC2 Ubuntu 24.04

1. Entra a la consola de AWS → servicio **EC2**.
2. Pulsa **Launch instance**.
3. Configura:
   - Nombre: `iot-monitor-ubuntu24` (o el que quieras).
   - AMI: busca **"Ubuntu Server 24.04 LTS"** (x86_64).
   - Tipo de instancia: `t2.micro` o `t3.micro` (sirve para pruebas).
   - Par de claves (key pair): selecciona uno existente o crea uno nuevo y descárgalo (`.pem`).
   - Red y subred: deja por defecto (VPC por defecto) para pruebas.
   - **Security group / Reglas de seguridad**:
     - SSH: puerto `22`, fuente: tu IP (por seguridad) o `0.0.0.0/0` solo para pruebas.
     - TCP personalizado: puerto `5000`, fuente: `0.0.0.0/0` (acceso al puerto de aplicación).
     - TCP personalizado: puerto `5001`, fuente: `0.0.0.0/0` (acceso HTTP).
4. Lanza la instancia y espera a que el estado sea **running**.
5. Anota la **IP pública** de la instancia.

---

## 2. Conectarse por SSH a la instancia (desde Windows)

En PowerShell de tu PC:

```powershell
cd C:\ruta\a\tu\clave
ssh -i .\mi-clave.pem ubuntu@IP_PUBLICA_EC2
```

- Acepta la huella cuando la pida (`yes`).
- Ya estás dentro como usuario `ubuntu`.

---

## 3. Instalar herramientas en Ubuntu 24.04

Dentro de la sesión SSH:

```bash
sudo apt-get update
sudo apt-get install -y git docker.io
```

Habilitar y arrancar Docker:

```bash
sudo systemctl enable --now docker
```

Permitir que el usuario `ubuntu` use Docker sin `sudo`:

```bash
sudo usermod -aG docker ubuntu
```

**IMPORTANTE:** salir y volver a entrar para que el grupo `docker` se aplique:

```bash
exit
```

Vuelve a conectar con SSH como en el paso 2.

Comprueba Docker:

```bash
docker run hello-world
```

Si ves el mensaje de bienvenida, Docker funciona correctamente.

---

## 4. Obtener el código del servidor en la instancia

### Opción A: clonar desde GitHub (recomendado)

1. Desde tu PC, asegúrate de haber subido los cambios del proyecto a GitHub.
2. En la instancia (SSH), dentro del home de `ubuntu`:

   ```bash
   cd ~
   git clone https://github.com/USUARIO/REPO.git iot-monitor
   cd iot-monitor
   ```

   Sustituye `USUARIO/REPO` por tu repositorio real.

### Opción B: copiar desde tu PC

Si no quieres usar GitHub, puedes comprimir la carpeta en tu PC y subirla con `scp`, pero para el proyecto se recomienda la opción A.

---

## 5. Revisar y construir la imagen Docker del servidor

Ya en la instancia, dentro del proyecto:

```bash
cd ~/iot-monitor/server
```

El archivo [server/Dockerfile](../server/Dockerfile) ya está preparado para **Ubuntu 24.04**:

- Imagen base: `ubuntu:24.04`.
- Instala `build-essential`.
- Copia `server.c` y `Makefile`.
- Compila el binario `server`.

Para construir la imagen:

```bash
docker build -t iot-monitor-server .
```

Esto tardará unos minutos la primera vez.

Puedes verificar las imágenes:

```bash
docker images
```

Deberías ver `iot-monitor-server` listado.

---

## 6. Ejecutar el contenedor del servidor

El binario `server` se ejecuta con:

```bash
./server puerto archivoDeLogs
```

En el Dockerfile ya se define un comando por defecto:

```dockerfile
CMD ["/bin/sh", "-c", "./server $PORT $LOG_FILE"]
```

Variables de entorno usadas dentro del contenedor:

- `PORT` → puerto de aplicación (por defecto `5000`).
- `LOG_FILE` → ruta del archivo de log (por defecto `/logs/server.log`).
- `AUTH_HOST` → nombre DNS del servicio externo de autenticación.
- `AUTH_PORT` → puerto del servicio de autenticación (por defecto `6000`).

### 6.1. Ejecutar sin servicio de autenticación (solo para probar arranque)

Si aún no tienes montado el servicio de autenticación, puedes lanzar el contenedor solo para ver que arranca (las conexiones de operadores fallarán en `LOGIN`, pero el servidor y HTTP funcionarán):

```bash
docker run -d \
  --name iot-monitor-server \
  -p 5000:5000 \
  -p 5001:5001 \
  -e PORT=5000 \
  -e LOG_FILE=/logs/server.log \
  iot-monitor-server
```

Verifica que el contenedor está corriendo:

```bash
docker ps
```

Mira los logs:

```bash
docker logs -f iot-monitor-server
```

Deberías ver algo como:

```text
Servidor de monitoreo IoT iniciado. Puerto aplicación: 5000, Puerto HTTP: 5001
```

Desde tu navegador en tu PC:

```text
http://IP_PUBLICA_EC2:5001/
```

Verás la página HTML del sistema de monitoreo (aunque sin sensores todavía).

---

## 7. Conectar el servidor con el servicio de autenticación

Para cumplir completamente el enunciado, el servidor debe validar a los operadores contra un servicio externo de autenticación.

### 7.1. Elegir dónde corre el servicio de autenticación

Tienes dos opciones:

1. **En la misma instancia EC2, fuera de Docker** (Python ejecutándose directamente en Ubuntu).  
2. **En otro contenedor Docker**.

La opción más simple para empezar es la **1**.

### 7.2. Ejecutar el servicio de autenticación en Ubuntu

En la instancia, abre una nueva sesión SSH (otra ventana o pestaña) y ejecuta:

```bash
cd ~/iot-monitor/clients/python
python3 auth_service.py
```

Esto dejará el servicio escuchando en el puerto `6000` en la máquina host.

Para dejarlo en segundo plano:

```bash
cd ~/iot-monitor/clients/python
nohup python3 auth_service.py >auth.log 2>&1 &
```

Comprueba que está escuchando:

```bash
ss -tlnp | grep 6000
```

### 7.3. Hacer que el servidor C resuelva el nombre del servicio de autenticación

El servidor C **no usa IP fija**, sino nombre de host (`AUTH_HOST`). Por defecto, en el código se usa `auth.iot.local` si no se define la variable.

Para que `auth.iot.local` resuelva a `127.0.0.1` (la misma instancia EC2), añade esta entrada en `/etc/hosts` del host Ubuntu:

```bash
echo "127.0.0.1 auth.iot.local" | sudo tee -a /etc/hosts
```

Ahora, cuando el contenedor intente resolver `auth.iot.local`, usará esa IP.

### 7.4. Relanzar el contenedor del servidor con variables de autenticación

Primero detén y elimina el contenedor anterior (si estaba corriendo):

```bash
docker stop iot-monitor-server
docker rm iot-monitor-server
```

Lanza de nuevo, pero indicando `AUTH_HOST` y `AUTH_PORT`:

```bash
docker run -d \
  --name iot-monitor-server \
  -p 5000:5000 \
  -p 5001:5001 \
  -e PORT=5000 \
  -e LOG_FILE=/logs/server.log \
  -e AUTH_HOST=auth.iot.local \
  -e AUTH_PORT=6000 \
  iot-monitor-server
```

Comprueba logs otra vez:

```bash
docker logs -f iot-monitor-server
```

Ahora, cuando un operador haga `LOGIN`, el servidor llamará al servicio Python (`auth_service.py`) usando DNS/hosts.

---

## 8. Probar desde fuera (sensores y operador)

Una vez que el servidor y el servicio de autenticación estén funcionando en AWS:

### 8.1. Sensores (en tu PC con Python 3)

```bash
cd "C:\\Users\\JuanD\\OneDrive\\Escritorio\\TELE 23\\clients\\python"
python sensor_simulator.py --host IP_PUBLICA_EC2 --port 5000
```

(Para la entrega final, en lugar de IP, usarás el nombre DNS configurado en Route 53, por ejemplo `iot-monitoring.example.com`).

### 8.2. Operador (en tu PC con Java)

```bash
cd "C:\\Users\\JuanD\\OneDrive\\Escritorio\\TELE 23\\clients\\java"
javac OperatorClient.java
java OperatorClient
```

- Host: `IP_PUBLICA_EC2` (o el nombre DNS final).
- Puerto: `5000`.
- Usuario: `operador1`.
- Clave: `clave1`.

Deberías poder:
- Conectar y autenticarte.
- Suscribirte a alertas.
- Ver la tabla de sensores.

---

## 9. Migrar de IP a DNS (AWS Route 53)

Para no usar IPs codificadas en los clientes, se recomienda:

1. Registrar un dominio o usar uno existente en Route 53.
2. Crear un registro **A**:
   - `iot-monitoring.example.com` → IP pública de la instancia EC2.
3. (Opcional) Crear otro registro o usar el mismo dominio para el servicio de autenticación; en el servidor ya estamos usando `auth.iot.local` mapeado a 127.0.0.1 por `/etc/hosts`, lo cual es aceptable para el laboratorio.

Luego, en los clientes, cambiar `IP_PUBLICA_EC2` por `iot-monitoring.example.com`.

---

## 10. Actualizar el servidor en el futuro

Cuando cambies el código del servidor (`server.c`, `Makefile` o `Dockerfile`):

1. Haz `git commit` y `git push` desde tu PC.
2. En la instancia EC2:

   ```bash
   cd ~/iot-monitor
   git pull
   cd server
   docker build -t iot-monitor-server .
   docker stop iot-monitor-server
   docker rm iot-monitor-server
   docker run -d \
     --name iot-monitor-server \
     -p 5000:5000 \
     -p 5001:5001 \
     -e PORT=5000 \
     -e LOG_FILE=/logs/server.log \
     -e AUTH_HOST=auth.iot.local \
     -e AUTH_PORT=6000 \
     iot-monitor-server
   ```

Con esto tendrás siempre la última versión del servidor ejecutándose en AWS sobre Ubuntu 24.04.
