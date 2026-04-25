import socket
import threading
from flask import Flask, render_template_string, jsonify
import webview

# Create a Flask web app
app = Flask(__name__)

# HTML template with JavaScript for live updates
html_template = """
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Data Display</title>
    <style>
      body { font-family: Arial, sans-serif; text-align: center; padding: 50px; }
      h1 { font-size: 48px; color: #333; }
      h2 { font-size: 24px; color: #666; margin-top: 20px; }
      button { font-size: 20px; padding: 10px 20px; margin: 5px; }
    </style>
    <script>
      // Poll the Flask API every second for live ESP32 data
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
      setInterval(updateVariables, 1000);  // Update every second
    </script>
  </head>
  <body>
    <h1><span id="variable">PASSEZ VOTRE TAG SUR LE LECTEUR</span></h1>
    <h2><span id="consommations"></span></h2>
    <h2>CHOISIS UN ROBINET</h2>
    <!-- Flow meter labels are updated dynamically from ESP32 data -->
    <button id="flowMeter1">Flow Meter 1</button>
    <button id="flowMeter2">Flow Meter 2</button>
  </body>
</html>
"""

# Variables to store the data received from ESP32
variable_value = "PASSEZ VOTRE TAG SUR LE LECTEUR"
consommations_value = ""  # Initially empty
flow_meter_names = ["", ""]  # Updated when ESP32 connects

@app.route('/')
def index():
    return render_template_string(html_template)

# API route to get the latest values of the variables
@app.route('/get_variables')
def get_variables():
    global variable_value, consommations_value, flow_meter_names
    return jsonify(
        variable=variable_value,
        consommations=consommations_value,
        flowMeter1=flow_meter_names[0],
        flowMeter2=flow_meter_names[1]
    )

# TCP server to receive data from ESP32
# NOTE: This implementation accepts a single connection. If the ESP32 disconnects,
# restart this script to re-establish the connection. A future improvement would
# be to add a reconnection loop around server_socket.accept().
def start_tcp_server():
    global variable_value, consommations_value, flow_meter_names
    HOST = '0.0.0.0'  # Listen on all network interfaces
    PORT = 12345       # Must match the port defined in nfc_tap.ino

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((HOST, PORT))
    server_socket.listen(1)

    print(f"Listening on {HOST}:{PORT}")
    client_socket, client_address = server_socket.accept()
    print(f"Connection from {client_address}")

    # Receive data continuously until the connection drops
    while True:
        try:
            data = client_socket.recv(1024).decode().strip()
            if data:
                # Expected format from ESP32: "firstName|consommations|flowMeter1Name|flowMeter2Name"
                parts = data.split("|")
                if len(parts) == 4:
                    variable_value    = f"BONJOUR {parts[0]}"  # Authenticated user's first name
                    consommations_value = parts[1]             # Remaining coupon count
                    flow_meter_names[0] = parts[2]             # Tap 1 label
                    flow_meter_names[1] = parts[3]             # Tap 2 label
                    print(f"Received: {variable_value}, {consommations_value}, {flow_meter_names[0]}, {flow_meter_names[1]}")
        except Exception as e:
            # Connection lost — dashboard will hold last known values until restart
            print(f"Connection lost: {e}")
            break

    client_socket.close()
    server_socket.close()

# Start the TCP server in a background thread so it doesn't block Flask
tcp_server_thread = threading.Thread(target=start_tcp_server)
tcp_server_thread.daemon = True  # Thread exits automatically when the main process stops
tcp_server_thread.start()

# Run Flask in a separate thread so it doesn't block the PyWebView window
flask_thread = threading.Thread(target=lambda: app.run(host='127.0.0.1', port=5000, debug=False))
flask_thread.daemon = True
flask_thread.start()

# Launch the dashboard as a native desktop window using PyWebView
def start_flask_in_window():
    webview.create_window("ESP32 Data Display", "http://127.0.0.1:5000", width=400, height=300)
    webview.start()

start_flask_in_window()
