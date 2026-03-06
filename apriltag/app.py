from flask import Flask, Response
from apriltag_engine import CameraTracker  # Importing your custom module!

app = Flask(__name__)

# Boot up the camera engine
engine = CameraTracker()

def generate_video():
    """Continuously asks the engine for a new frame and yields it to Flask."""
    while True:
        frame_bytes = engine.get_frame_stream()
        if frame_bytes is not None:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

@app.route('/')
def video_feed():
    return Response(generate_video(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == "__main__":
    print("Starting Flask server... Connect via your browser.")
    try:
        app.run(host='0.0.0.0', port=5000, debug=False)
    finally:
        # Ensure the camera hardware is safely released when you stop the server
        engine.release()
