import socket
import threading
from flask import Flask, render_template_string, jsonify
import webview

app = Flask(__name__)

html_template = """
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>SmartTap Live Dashboard</title>
    <style>
      body { font-family: Arial, sans-serif; text-align: center; padding: 50px; }
      h1 { font-size: 48px; color: #333; }
      h2 { font-size: 24px; color: #666; margin-top: 20px; }
      button { font-size: 20px; padding: 10px 20px; margin: 5px; }
    </style>
    <script>
      function updateVariables() {
        fetch('/get_variables')
          .then(response => response.json())
          .then(data => {
            document.getElementById('variable').innerText = data.variable;
            document.getElementById('consommations').innerText = data.consommations;
            document.getElementById('flowMeter1').innerText = data.flowMeter1;
            document.getElementById('flowMeter2').innerText = data.flowMeter2;
          });
      }
      setInterval(updateVariables, 1000);
      window.addEventListener('load', updateVariables);
    </script>
  </head>
  <body>
    <h1><span id="variable">PASSEZ VOTRE TAG SUR LE LECTEUR</span></h1>
    <h2><span id="consommations"></span></h2>
    <h2>CHOISIS UN ROBINET</h2>
    <button id="flowMeter1">Flow Meter 1</button>
    <button id="flowMeter2">Flow Meter 2</button>
  </body>
</html>
"""

variable_value = "PASSEZ VOTRE TAG SUR LE LECTEUR"
consommations_value = ""
flow_meter_names = ["Flow Meter 1", "Flow Meter 2"]
state_lock = threading.Lock()


@app.route('/')
def index():
    return render_template_string(html_template)


@app.route('/get_variables')
def get_variables():
    with state_lock:
        return jsonify(
            variable=variable_value,
            consommations=consommations_value,
            flowMeter1=flow_meter_names[0],
            flowMeter2=flow_meter_names[1]
        )


def apply_esp32_message(message: str) -> None:
    """Parse one ESP32 dashboard payload.

    Expected format:
        firstName|consommations|flowMeter1Name|flowMeter2Name
    """
    global variable_value, consommations_value, flow_meter_names

    parts = message.strip().split("|", 3)
    if len(parts) != 4:
        print(f"Ignoring malformed payload: {message!r}")
        return

    first_name, consumptions, flow_1, flow_2 = parts
    with state_lock:
        variable_value = f"BONJOUR {first_name}"
        consommations_value = consumptions
        flow_meter_names[0] = flow_1
        flow_meter_names[1] = flow_2

    print(f"Received: {variable_value}, {consommations_value}, {flow_1}, {flow_2}")


def handle_client(client_socket: socket.socket, client_address) -> None:
    print(f"Connection from {client_address}")
    with client_socket:
        while True:
            data = client_socket.recv(1024)
            if not data:
                print(f"Connection closed by {client_address}")
                break
            for line in data.decode(errors="replace").splitlines():
                if line.strip():
                    apply_esp32_message(line)


def start_tcp_server():
    host = '0.0.0.0'
    port = 12345

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind((host, port))
        server_socket.listen(1)
        print(f"Listening on {host}:{port}")

        while True:
            client_socket, client_address = server_socket.accept()
            try:
                handle_client(client_socket, client_address)
            except Exception as exc:
                print(f"Connection error from {client_address}: {exc}")


tcp_server_thread = threading.Thread(target=start_tcp_server, daemon=True)
tcp_server_thread.start()

flask_thread = threading.Thread(
    target=lambda: app.run(host='127.0.0.1', port=5000, debug=False, use_reloader=False),
    daemon=True
)
flask_thread.start()


def start_flask_in_window():
    webview.create_window("SmartTap Live Dashboard", "http://127.0.0.1:5000", width=500, height=350)
    webview.start()


start_flask_in_window()
