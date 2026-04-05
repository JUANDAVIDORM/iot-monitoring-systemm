# Despliegue en AWS (Ubuntu + Docker) y uso de Git

Este documento describe, paso a paso, como desplegar el sistema en AWS y como manejar el repositorio Git.

## 1. Preparar el repositorio local (Windows)

1. Asegurate de tener Git instalado.
2. En una terminal PowerShell:

   ```powershell
   cd "C:\Users\JuanD\OneDrive\Escritorio\TELE 23"
   git init
   git add .
   git commit -m "Initial commit - IoT monitoring system"
   ```

3. Crea un repositorio vacio en GitHub (privado, como pide el enunciado).
4. Copia la URL `https://github.com/usuario/repositorio.git`.
5. En PowerShell:

   ```powershell
   git remote add origin https://github.com/usuario/repositorio.git
   git branch -M main
   git push -u origin main
   ```

Cada vez que hagas cambios:

```powershell
git status
git add .
git commit -m "Descripcion corta del cambio"
git push
```

## 2. Crear instancia EC2 Ubuntu en AWS

1. Entra a la consola de AWS y ve a **EC2**.
2. Lanza una nueva instancia:
   - AMI: **Ubuntu Server 22.04 LTS**.
   - Tipo: `t2.micro` o `t3.micro` (free tier si aplica).
   - Par de claves: crea o reutiliza uno (para SSH).
   - Grupo de seguridad: abre estos puertos de entrada:
     - `22` (SSH, desde tu IP).
     - `5000` (puerto de aplicacion TCP del servidor).
     - `5001` (puerto HTTP).
3. Crea y lanza la instancia.

## 3. Conectarse por SSH desde Windows

En PowerShell (ajusta la ruta a tu `.pem` y la IP publica de tu instancia):

```powershell
ssh -i C:\ruta\a\mi-clave.pem ubuntu@IP_PUBLICA_EC2
```

Acepta la huella cuando lo pida.

## 4. Instalar Docker y Git en Ubuntu

En la sesion SSH:

```bash
sudo apt-get update
sudo apt-get install -y git docker.io python3 python3-pip
sudo systemctl enable --now docker
sudo usermod -aG docker ubuntu
```

Cierra sesion (`exit`) y vuelve a entrar por SSH para que el grupo `docker` se aplique.

Verifica Docker:

```bash
docker run hello-world
```

## 5. Clonar el repositorio en la instancia

En la sesion SSH, dentro del home de `ubuntu`:

```bash
git clone https://github.com/usuario/repositorio.git iot-monitor
cd iot-monitor
```

Asegurate de ajustar la URL a la de tu repo real.

## 6. Configurar el servicio de autenticacion

1. Dentro del directorio del proyecto en la instancia:

   ```bash
   cd clients/python
   python3 auth_service.py
   ```

   Esto dejara el servicio escuchando en el puerto `6000` (bloquea la terminal).

2. Para dejarlo en segundo plano, usa otra terminal SSH o ejecuta:

   ```bash
   nohup python3 auth_service.py >auth.log 2>&1 &
   ```

3. Asegurate que el nombre DNS que usara el servidor C para autenticacion resuelva a la misma maquina.
   - Por defecto, el servidor usa `AUTH_HOST=auth.iot.local`.
   - Mientras aun no configuras Route 53, puedes agregar en Ubuntu:

     ```bash
     echo "127.0.0.1 auth.iot.local" | sudo tee -a /etc/hosts
     ```

## 7. Construir la imagen Docker del servidor C

Desde la raiz del proyecto (donde esta la carpeta `server`):

```bash
cd server
docker build -t iot-monitor-server .
```

Esto compila `server.c` dentro de la imagen.

## 8. Ejecutar el contenedor del servidor

1. Ejecutar el contenedor exponiendo puertos:

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

2. Verificar que este corriendo:

   ```bash
   docker ps
   docker logs -f iot-monitor-server
   ```

Deberias ver un mensaje similar a:

```text
Servidor de monitoreo IoT iniciado. Puerto aplicación: 5000, Puerto HTTP: 5001
```

## 9. Probar acceso HTTP desde tu navegador

1. En tu PC, abre:

   ```text
   http://IP_PUBLICA_EC2:5001/
   ```

2. Deberias ver la pagina HTML con el titulo "Sistema de Monitoreo IoT" y la tabla (inicialmente vacia) de sensores.

## 10. Configurar DNS con Route 53

1. Crea un dominio o usa uno existente en Route 53.
2. Crea un registro `A` o `CNAME` por ejemplo:
   - `iot-monitoring.example.com` -> IP publica de la instancia EC2.
   - `auth.iot.local` o `auth.example.com` -> misma IP.
3. Espera a que la propagacion DNS se complete (puede tardar varios minutos).

Luego, desde tu PC, deberias poder acceder a:

- `http://iot-monitoring.example.com:5001/`

Los clientes se conectaran usando esos nombres de host (sin IPs codificadas).

## 11. Ejecutar clientes de sensores (Windows)

En tu maquina Windows (no en AWS):

1. Asegurate de tener Python 3 instalado.
2. En PowerShell:

   ```powershell
   cd "C:\Users\JuanD\OneDrive\Escritorio\TELE 23\clients\python"
   python sensor_simulator.py --host iot-monitoring.example.com --port 5000
   ```

3. Veras mensajes de envio de `DATA` y respuestas `OK DATA RECEIVED`.
4. La pagina HTTP en AWS mostrara ahora sensores activos y sus ultimas mediciones.

## 12. Ejecutar cliente de operador con interfaz grafica (Windows)

1. Instala Java (JDK 8+).
2. En PowerShell:

   ```powershell
   cd "C:\Users\JuanD\OneDrive\Escritorio\TELE 23\clients\java"
   javac OperatorClient.java
   java OperatorClient
   ```

3. En la ventana:
   - Host: `iot-monitoring.example.com`.
   - Puerto: `5000`.
   - Usuario: `operador1`.
   - Clave: `clave1`.
4. Pulsa **Conectar**.
5. Pulsa **Suscribirse a alertas**.
6. Pulsa **Actualizar sensores** para solicitar la lista actual.
7. Cuando se produzcan valores fuera de rango, apareceran lineas `ALERT ...` en el area inferior.

## 13. Mantener el sistema en ejecucion

- Para ver logs del servidor:

  ```bash
  docker logs -f iot-monitor-server
  ```

- Para reiniciar el servidor:

  ```bash
  docker restart iot-monitor-server
  ```

- Para actualizar la version:
  1. `git pull` en la instancia EC2.
  2. `docker build -t iot-monitor-server .` dentro de `server`.
  3. `docker stop iot-monitor-server && docker rm iot-monitor-server`.
  4. Lanzar de nuevo `docker run ...` como en la seccion 8.

## 14. Notas sobre errores comunes

- **Puerto ya en uso**: revisa con `docker ps` o `sudo lsof -i:5000` si ya hay un proceso ocupando el puerto.
- **Autenticacion siempre falla**: revisa que `AUTH_HOST` resuelva al host donde corre `auth_service.py` y que el puerto `6000` este abierto localmente.
- **No se puede conectar desde Windows**: verifica reglas del grupo de seguridad de EC2 y que el firewall local no bloquee los puertos 5000 y 5001.
- **Problemas con DNS**: mientras pruebas, puedes usar el nombre configurado en `/etc/hosts` o en `C:\Windows\System32\drivers\etc\hosts` en tu PC, pero para la entrega final se recomienda usar Route 53.
