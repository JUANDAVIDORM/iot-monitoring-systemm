import argparse
import random
import socket
import threading
import time
from datetime import datetime


def connect_and_send(sensor_id: str, sensor_type: str, host: str, port: int, interval: float = 2.0):
    while True:
        try:
            with socket.create_connection((host, port)) as s:
                f = s.makefile("rw", buffering=1, encoding="utf-8")
                f.write(f"HELLO SENSOR {sensor_id} {sensor_type}\n")
                welcome = f.readline().strip()
                print(f"[{sensor_id}] Respuesta: {welcome}")
                if not welcome.startswith("OK"):
                    time.sleep(5)
                    continue

                while True:
                    if sensor_type == "TEMP":
                        value = random.uniform(10.0, 90.0)
                    elif sensor_type == "VIB":
                        value = random.uniform(0.0, 8.0)
                    else:  # ENERGY
                        value = random.uniform(100.0, 1200.0)
                    ts = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
                    msg = f"DATA {sensor_id} {value:.2f} {ts}\n"
                    f.write(msg)
                    resp = f.readline().strip()
                    print(f"[{sensor_id}] -> {msg.strip()} | <- {resp}")
                    time.sleep(interval)
        except Exception as e:
            print(f"[{sensor_id}] Error de conexion: {e}. Reintentando en 5s...")
            time.sleep(5)


def main():
    parser = argparse.ArgumentParser(description="Simulador de sensores IoT")
    parser.add_argument("--host", required=True, help="Nombre de host del servidor de monitoreo (DNS)")
    parser.add_argument("--port", type=int, required=True, help="Puerto de aplicacion del servidor de monitoreo")
    args = parser.parse_args()

    sensors = [
        ("sensor_temp_1", "TEMP"),
        ("sensor_temp_2", "TEMP"),
        ("sensor_vib_1", "VIB"),
        ("sensor_energy_1", "ENERGY"),
        ("sensor_energy_2", "ENERGY"),
    ]

    threads = []
    for sensor_id, sensor_type in sensors:
        t = threading.Thread(
            target=connect_and_send,
            args=(sensor_id, sensor_type, args.host, args.port),
            daemon=True,
        )
        t.start()
        threads.append(t)

    # Mantener proceso vivo
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Saliendo del simulador de sensores...")


if __name__ == "__main__":
    main()
