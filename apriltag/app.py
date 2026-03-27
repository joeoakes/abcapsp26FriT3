from flask import Flask, Response
from apriltag_engine import CameraTracker, find_cameras
import time

app = Flask(__name__)

# -----------------------------
# Camera Selection (runs once at startup)
# -----------------------------
def select_camera():
    cams = find_cameras()

    if not cams:
        raise RuntimeError("No cameras detected.")

    print("\nAvailable cameras:")
    for cam in cams:
        print(f" - Camera {cam}")

    # Auto-pick if only one camera
    if len(cams) == 1:
        print(f"[INFO] Only one camera found. Using {cams[0]}")
        return cams[0]

    # Ask user
    while True:
        try:
            cam_index = int(input("Select camera index: "))
            if cam_index in cams:
                return cam_index
            else:
                print("Invalid selection. Try again.")
        except ValueError:
            print("Please enter a valid number.")

# Initialize camera engine AFTER selection
cam_index = select_camera()
engine = CameraTracker(cam_index)


# -----------------------------
# Video Streaming Generators
# -----------------------------
def stream_video():
    """Continuously yields the latest camera frame."""
    while True:
        frame_bytes = engine.latest_frame
        if frame_bytes is not None:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')
        time.sleep(0.03)

def stream_map():
    """Continuously yields the latest grid map frame."""
    while True:
        map_bytes = engine.grid_map.get_map_stream()
        if map_bytes is not None:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + map_bytes + b'\r\n')
        time.sleep(0.03)


# -----------------------------
# Routes
# -----------------------------
@app.route('/')
def dashboard():
    html = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>Robot Vision Dashboard</title>
        <style>
            body { font-family: Arial, sans-serif; background: #222; color: #fff; text-align: center; }
            .container { display: flex; justify-content: center; gap: 30px; margin-top: 20px; flex-wrap: wrap; }
            .feed { background: #333; padding: 10px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
            img { border: 2px solid #555; border-radius: 4px; max-width: 100%; height: auto; }
        </style>
    </head>
    <body>
        <h2>Multi-Tag Mapping Dashboard</h2>
        <div class="container">
            <div class="feed">
                <h3>Live Camera</h3>
                <img src="/video_feed" width="640">
            </div>
            <div class="feed">
                <h3>2D Grid Map</h3>
                <img src="/map_feed" width="600">
            </div>
        </div>
    </body>
    </html>
    """
    return html

@app.route('/video_feed')
def video_feed():
    return Response(stream_video(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/map_feed')
def map_feed():
    return Response(stream_map(), mimetype='multipart/x-mixed-replace; boundary=frame')


# -----------------------------
# Run Server
# -----------------------------
if __name__ == "__main__":
    print("\nStarting Flask server...")
    print("Open your browser at: http://localhost:5000\n")

    try:
        app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
    finally:
        print("\n[INFO] Shutting down camera...")
        engine.release()