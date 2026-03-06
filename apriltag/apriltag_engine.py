import cv2
import numpy as np
from pupil_apriltags import Detector
import math

class CameraTracker:
    def __init__(self, cam_index=0):
        # Initialize camera
        self.cap = cv2.VideoCapture(cam_index, cv2.CAP_V4L2)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        
        if not self.cap.isOpened():
            raise RuntimeError("Could not open camera. Please check connections.")

        # Initialize detector WITH the precision image filters restored
        self.detector = Detector(
            families="tag36h11",
            nthreads=2,
            quad_decimate=2.0,
            quad_sigma=0.0,
            refine_edges=1,
            decode_sharpening=0.25,
            debug=0
        )
        
        # Calibration parameters
        self.tag_size_m = 0.152  
        w, h = 1280, 720
        self.camera_params = (900.0, 900.0, w / 2.0, h / 2.0)

    def get_frame_stream(self):
        """Reads a frame, processes the AprilTag, and returns a JPEG buffer."""
        ok, frame = self.cap.read()
        if not ok:
            return None

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        tags = self.detector.detect(
            gray,
            estimate_tag_pose=True,
            camera_params=self.camera_params,
            tag_size=self.tag_size_m
        )

        best = max(tags, key=lambda t: t.decision_margin) if tags else None

        if best is not None:
            tag_id = best.tag_id
            tx, ty, tz = best.pose_t.flatten().tolist()

            # Calculate rotation
            R = best.pose_R
            yaw = math.atan2(R[1,0], R[0,0])
            pitch = math.atan2(-R[2,0], math.sqrt(R[2,1]**2 + R[2,2]**2))
            roll = math.atan2(R[2,1], R[2,2])
            roll, pitch, yaw = map(math.degrees, (roll, pitch, yaw))

            # Draw the green bounding box
            corners = best.corners.astype(int)
            for i in range(4):
                cv2.line(frame, tuple(corners[i]), tuple(corners[(i+1) % 4]), (0, 255, 0), 2)

            # Draw the coordinates on the video feed
            msg1 = f"ID: {tag_id}"
            msg2 = f"X:{tx:.2f}m Y:{ty:.2f}m Z:{tz:.2f}m"
            cv2.putText(frame, msg1, (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
            cv2.putText(frame, msg2, (20, 75), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)

            # Print to the SSH terminal so you know it is working
            print(f"Detected ID: {tag_id} | X: {tx:.2f}m, Y: {ty:.2f}m, Z: {tz:.2f}m")

        # Encode to JPEG for the web server
        ret, buffer = cv2.imencode('.jpg', frame)
        return buffer.tobytes()

    def release(self):
        self.cap.release()
