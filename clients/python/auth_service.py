import socket
import threading

HOST = "0.0.0.0"  # Se resolverá por nombre en el cliente, aquí solo se escucha en todas las interfaces
PORT = 6000

# Base de usuarios de ejemplo (no se almacena en el servidor C)
USERS = {
    "operador1": {"password": "clave1", "role": "operator"},
    "operador2": {"password": "clave2", "role": "operator"},
}


def handle_client(conn, addr):
    try:
        data = b""
        while not data.endswith(b"\n"):
            chunk = conn.recv(1024)
            if not chunk:
                return
            data += chunk
        line = data.decode("utf-8", errors="ignore").strip()
        # Formato esperado: AUTH usuario password
        parts = line.split()
        if len(parts) != 3 or parts[0] != "AUTH":
            conn.sendall(b"ERROR Invalid AUTH format\n")
            return
        _, username, password = parts
        user = USERS.get(username)
        if user and user["password"] == password:
            response = f"OK ROLE {user['role']}\n".encode("utf-8")
        else:
            response = b"ERROR Authentication failed\n"
        conn.sendall(response)
    finally:
        conn.close()


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen(5)
        print(f"Servicio de autenticacion escuchando en puerto {PORT}...")
        while True:
            conn, addr = s.accept()
            t = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            t.start()


if __name__ == "__main__":
    main()
