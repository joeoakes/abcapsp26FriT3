from flask import Flask, Response
from apriltag_engine import CameraTracker, find_cameras

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
# Video Streaming Generator
# -----------------------------
def generate_video():
    """Continuously asks the engine for a new frame and yields it to Flask."""
    while True:
        frame_bytes = engine.get_frame_stream()
        if frame_bytes is not None:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')


# -----------------------------
# Routes
# -----------------------------
@app.route('/')
def video_feed():
    return Response(generate_video(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')


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