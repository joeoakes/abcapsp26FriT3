import cv2
import numpy as np
from pupil_apriltags import Detector
import math
import platform

# -----------------------------
# Camera Utilities
# -----------------------------
def get_backend():
    system = platform.system()
    if system == "Windows":
        return cv2.CAP_DSHOW
    elif system == "Linux":
        return cv2.CAP_V4L2
    else:
        return cv2.CAP_ANY


def find_cameras(max_tests=5):
    backend = get_backend()
    available = []

    print("[INFO] Scanning for cameras...")

    for i in range(max_tests):
        cap = cv2.VideoCapture(i, backend)
        if cap.isOpened():
            print(f"[INFO] Camera found at index {i}")
            available.append(i)
            cap.release()

    if not available:
        print("[ERROR] No cameras detected.")

    return available


def open_camera(cam_index):
    backend = get_backend()

    cap = cv2.VideoCapture(cam_index, backend)

    if cap.isOpened():
        print(f"[INFO] Opened camera {cam_index} with backend {backend}")
        try:
            print("[INFO] Backend name:", cap.getBackendName())
        except:
            pass
        return cap

    raise RuntimeError(f"Failed to open camera index {cam_index}")


# -----------------------------
# Camera Tracker Class
# -----------------------------
class CameraTracker:
    def __init__(self, cam_index=None):
        # Detect cameras
        cams = find_cameras()

        if not cams:
            raise RuntimeError("No cameras detected.")

        # Choose camera
        if cam_index is None:
            cam_index = cams[0]  # default to first camera
        elif cam_index not in cams:
            print(f"[WARNING] Camera {cam_index} not found. Using {cams[0]} instead.")
            cam_index = cams[0]

        print(f"[INFO] Using camera index: {cam_index}")

        # Open camera
        self.cap = open_camera(cam_index)

        # Set resolution
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

        # -----------------------------
        # AprilTag Detector
        # -----------------------------
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

    # -----------------------------
    # Frame Processing
    # -----------------------------
    def get_frame_stream(self):
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

            # Rotation
            R = best.pose_R
            yaw = math.atan2(R[1, 0], R[0, 0])
            pitch = math.atan2(-R[2, 0], math.sqrt(R[2, 1]**2 + R[2, 2]**2))
            roll = math.atan2(R[2, 1], R[2, 2])
            roll, pitch, yaw = map(math.degrees, (roll, pitch, yaw))

            # Draw bounding box
            corners = best.corners.astype(int)
            for i in range(4):
                cv2.line(frame, tuple(corners[i]), tuple(corners[(i + 1) % 4]), (0, 255, 0), 2)

            # Overlay text
            msg1 = f"ID: {tag_id}"
            msg2 = f"X:{tx:.2f}m Y:{ty:.2f}m Z:{tz:.2f}m"

            cv2.putText(frame, msg1, (20, 40),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)

            cv2.putText(frame, msg2, (20, 75),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)

            print(f"[DETECTED] ID: {tag_id} | X:{tx:.2f} Y:{ty:.2f} Z:{tz:.2f}")

        # Encode frame for Flask
        ret, buffer = cv2.imencode('.jpg', frame)
        return buffer.tobytes() if ret else None

    # -----------------------------
    # Cleanup
    # -----------------------------
    def release(self):
        if self.cap:
            self.cap.release()
            print("[INFO] Camera released")