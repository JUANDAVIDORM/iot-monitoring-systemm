# Protocolo de Aplicacion - Sistema de Monitoreo IoT

Este documento resume el protocolo de texto usado entre sensores, operadores y el servidor central.

## Consideraciones generales

- Transporte: TCP (sockets de flujo / SOCK_STREAM).
- Codificacion: UTF-8.
- Terminacion de linea: `\n` (LF), el servidor tolera `\r\n`.
- Mensajes basados en texto ASCII separados por espacios.

## Tipos de clientes

- **Sensores**: envian mediciones periodicas.
- **Operadores**: supervisan el sistema, consultan estado y reciben alertas.

## Mensajes de sensores

1. **Handshake inicial**

   ```text
   HELLO SENSOR <sensor_id> <sensor_type>\n
   ```

   - `sensor_id`: identificador sin espacios.
   - `sensor_type`: `TEMP`, `VIB` o `ENERGY`.

   Respuesta del servidor:

   ```text
   OK WELCOME SENSOR\n
   ```

2. **Envio de mediciones**

   ```text
   DATA <sensor_id> <value> <timestamp>\n
   ```

   - `value`: numero en punto flotante.
   - `timestamp`: `YYYY-MM-DDThh:mm:ssZ` (UTC).

   Respuesta exitosa:

   ```text
   OK DATA RECEIVED\n
   ```

   Errores posibles:

   - `ERROR Invalid DATA format`
   - `ERROR Too many sensors`

3. **Finalizacion**

   ```text
   QUIT\n
   ```

   Respuesta:

   ```text
   OK BYE\n
   ```

## Mensajes de operadores

1. **Handshake y autenticacion**

   Mensaje inicial:

   ```text
   HELLO OPERATOR\n
   ```

   Respuesta del servidor:

   ```text
   AUTH REQUIRED\n
   ```

   Luego el cliente envia:

   ```text
   LOGIN <username> <password>\n
   ```

   El servidor consulta al servicio externo de autenticacion mediante:

   ```text
   AUTH <username> <password>\n
   ```

   Respuesta esperada del servicio de autenticacion:

   ```text
   OK ROLE <role>\n
   ```

   Si es exitoso:

   ```text
   OK LOGIN\n
   ```

   En caso de fallo:

   ```text
   ERROR Authentication failed\n
   ```

2. **Suscripcion a alertas**

   ```text
   SUBSCRIBE ALERTS\n
   ```

   Respuesta:

   ```text
   OK SUBSCRIBED\n
   ```

   A partir de este momento, el servidor puede enviar asíncronamente lineas de alerta:

   ```text
   ALERT <sensor_id> <sensor_type> <value> <timestamp>\n
   ```

3. **Consulta de sensores activos**

   ```text
   GET SENSORS\n
   ```

   Respuesta: una lista de sensores seguida de `END`.

   ```text
   SENSOR <id> <type> <last_value> <last_timestamp>\n
   ...
   END\n
   ```

4. **Finalizacion**

   ```text
   QUIT\n
   ```

   Respuesta:

   ```text
   OK BYE\n
   ```

## Servicio de autenticacion externo

- Puerto por defecto: `6000`.
- Protocolo de texto:

  ```text
  AUTH <username> <password>\n
  ```

- Respuestas posibles:

  ```text
  OK ROLE <role>\n
  ERROR Authentication failed\n
  ERROR Invalid AUTH format\n
  ```

El servidor C descubre el host y puerto de este servicio con las variables de entorno:

- `AUTH_HOST` (por defecto `auth.iot.local`, nombre de dominio, no IP).
- `AUTH_PORT` (por defecto `6000`).

## Interfaz HTTP

- Puerto HTTP: `puerto_aplicacion + 1`.
- Solo maneja peticiones `GET`.
- La ruta `/` devuelve una pagina HTML basica con:
  - Puertos de aplicacion y HTTP.
  - Tabla de sensores activos y su ultima medicion.

Ejemplo de respuesta HTTP:

```http
HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Content-Length: ...
Connection: close

<html>...</html>
```
