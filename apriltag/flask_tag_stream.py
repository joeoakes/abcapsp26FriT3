import cv2
import numpy as np
from pupil_apriltags import Detector
import math
from flask import Flask, Response

app = Flask(__name__)

# ---- Camera & Detector Setup ----
cam_index = 0
cap = cv2.VideoCapture(cam_index, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

detector = Detector(
    families="tag36h11",
    nthreads=2,
    quad_decimate=2.0,
    quad_sigma=0.0,
    refine_edges=1,
    decode_sharpening=0.25,
    debug=0
)

tag_size_m = 0.152  

w, h = 1280, 720
fx = fy = 900.0
cx = w / 2.0
cy = h / 2.0

def generate_frames():
    """Continuously reads frames, detects tags, and yields them as JPEG streams."""
    while True:
        ok, frame = cap.read()
        if not ok:
            break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        tags = detector.detect(
            gray,
            estimate_tag_pose=True,
            camera_params=(fx, fy, cx, cy),
            tag_size=tag_size_m
        )

        best = None
        if tags:
            best = max(tags, key=lambda t: t.decision_margin)

        if best is not None:
            tag_id = best.tag_id
            tx, ty, tz = best.pose_t.flatten().tolist()

            R = best.pose_R
            yaw = math.atan2(R[1,0], R[0,0])
            pitch = math.atan2(-R[2,0], math.sqrt(R[2,1]**2 + R[2,2]**2))
            roll = math.atan2(R[2,1], R[2,2])
            roll, pitch, yaw = map(math.degrees, (roll, pitch, yaw))

            # ---- Draw Visuals on the Frame ----
            # Draw bounding box
            corners = best.corners.astype(int)
            for i in range(4):
                p0 = tuple(corners[i])
                p1 = tuple(corners[(i+1) % 4])
                cv2.line(frame, p0, p1, (0, 255, 0), 2)

            # Draw text
            msg1 = f"ID: {tag_id}"
            msg2 = f"X:{tx:.2f}m Y:{ty:.2f}m Z:{tz:.2f}m"
            cv2.putText(frame, msg1, (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
            cv2.putText(frame, msg2, (20, 75), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)

            # Keep printing to SSH terminal for logging
            print(f"ID: {tag_id} | Z: {tz:.2f}m | Roll: {roll:.0f}, Pitch: {pitch:.0f}, Yaw: {yaw:.0f}")

        # Encode the frame into a JPEG memory buffer
        ret, buffer = cv2.imencode('.jpg', frame)
        frame_bytes = buffer.tobytes()

        # Yield the frame in the format required for MJPEG streaming
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

@app.route('/')
def video_feed():
    # Route the generator function to the root URL
    return Response(generate_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == "__main__":
    if not cap.isOpened():
        print("Error: Could not open camera.")
    else:
        print("Starting Flask server... Connect via your browser.")
        # host='0.0.0.0' allows external devices on the same Wi-Fi to connect
        app.run(host='0.0.0.0', port=5000, debug=False)
