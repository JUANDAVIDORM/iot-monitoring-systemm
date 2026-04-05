# Sistema Distribuido de Monitoreo de Sensores IoT

Proyecto académico de la asignatura **Internet: Arquitectura y Protocolos** (Telemática), que implementa un sistema distribuido de monitoreo de sensores IoT desplegado en la nube.

---

## Información del proyecto

- **Universidad:** Universidad EAFIT
- **Ciudad:** Medellín, Colombia
- **Programa:** Ingeniería (Telemática / Afines)
- **Curso:** Internet: Arquitectura y Protocolos – 2026-1
- **Proyecto:** Sistema Distribuido de Monitoreo de Sensores IoT
- **Fecha:** 5 de abril de 2026

### Integrantes

- Juan David Ortiz Moncada
- Alexander Vargas Atehortúa
- Sebastián Martínez Maya

---

## Descripción general

El sistema simula una plataforma de monitoreo industrial donde varios sensores IoT envían mediciones a un servidor central en la nube, y operadores humanos supervisan el estado del sistema mediante un cliente de control.

El proyecto integra varios conceptos vistos en el curso:

- Sockets Berkeley (TCP) y programación concurrente en C.
- Diseño e implementación de un **protocolo de aplicación basado en texto**.
- Clientes desarrollados en **múltiples lenguajes** (Python y Java).
- **Servicio de autenticación externo** (no acoplado al servidor principal).
- **Interfaz web HTTP** básica para visualizar el estado del sistema.
- Despliegue en **AWS (Ubuntu + Docker)**, evitando direcciones IP fijas mediante resolución de nombres.

---

## Arquitectura de alto nivel

Componentes principales:

1. **Servidor central de monitoreo (C)**
	- Implementado en C usando sockets Berkeley y `pthread`.
	- Recibe registros y mediciones de sensores.
	- Mantiene en memoria el estado de sensores activos.
	- Detecta condiciones anómalas y genera alertas.
	- Notifica en tiempo real a operadores suscritos.
	- Expone una interfaz HTTP sencilla para visualizar sensores activos.

2. **Servicio de autenticación externo (Python)**
	- Expone un protocolo de texto simple (`AUTH <usuario> <clave>`).
	- Devuelve rol del usuario (`OK ROLE operator` o error).
	- El servidor C nunca almacena usuarios localmente; siempre consulta este servicio.

3. **Sensores IoT simulados (Python)**
	- Múltiples procesos/hilos que simulan sensores de **temperatura**, **vibración** y **energía**.
	- Se conectan al servidor C usando TCP.
	- Envían periódicamente mensajes `DATA` con mediciones y timestamp en formato ISO.

4. **Cliente operador con interfaz gráfica (Java)**
	- Implementado con **Java Swing**.
	- Permite:
	  - Conectarse y autenticarse como operador.
	  - Suscribirse a alertas del sistema.
	  - Consultar la lista de sensores activos y sus últimas mediciones.

5. **Contenedor Docker del servidor**
	- Imagen basada en `ubuntu:24.04`.
	- Compila el servidor C dentro del contenedor.
	- Expone los puertos de aplicación (`5000`) y HTTP (`5001`).
	- Preparado para despliegue en una instancia EC2 de AWS.

---

## Tecnologías utilizadas

- **Lenguajes:** C, Python 3, Java.
- **Red y concurrencia:** Sockets TCP, API Berkeley, `pthread` (hilos).
- **Interfaz gráfica:** Java Swing.
- **Contenedores:** Docker.
- **Infraestructura en la nube:** AWS EC2 (Ubuntu 24.04).

---

## Documentación

Para detalles completos del protocolo, despliegue y uso del sistema:

- Especificación del protocolo de aplicación: [docs/protocol.md](docs/protocol.md)
- Guía detallada de despliegue en AWS (Ubuntu + Docker): [docs/deployment_aws_git.md](docs/deployment_aws_git.md)
- Instrucciones paso a paso para ejecutar todo en AWS: [docs/instrucciones_aws.txt](docs/instrucciones_aws.txt)

---

## Resumen de ejecución (local)

> Nota: los comandos exactos, parámetros y consideraciones están documentados en `docs/`. Aquí solo se muestra una visión rápida.

1. **Servicio de autenticación (Python):**
	- `cd clients/python`
	- `python auth_service.py`

2. **Servidor C (local, sin Docker):**
	- `cd server`
	- `make`
	- `./server 5000 logs.txt`

3. **Simulador de sensores (Python):**
	- `cd clients/python`
	- `python sensor_simulator.py --host <host_servidor> --port 5000`

4. **Cliente operador (Java):**
	- `cd clients/java`
	- `javac OperatorClient.java`
	- `java OperatorClient`

Para despliegue en AWS, consulta las guías en la carpeta `docs/`.
