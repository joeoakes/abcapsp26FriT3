import cv2
import numpy as np
from pupil_apriltags import Detector
import math
import platform

# -----------------------------
# Grid Map Class
# -----------------------------
class GridMap:
    def __init__(self, size=200, resolution=0.05):
        self.size = size
        self.resolution = resolution
        self.center = size // 2
        self.grid = np.zeros((size, size), dtype=np.uint8)

    def update(self, x, y, strength=25):
        """Convert world coordinates (meters) to grid indices and update."""
        gx = int(x / self.resolution) + self.center
        gy = int(y / self.resolution) + self.center

        if 0 <= gx < self.size and 0 <= gy < self.size:
            self.grid[gy, gx] = min(self.grid[gy, gx] + strength, 255)

    def get_map_stream(self):
        """Fades the map slightly, draws the robot, and encodes as JPEG."""
        # Slowly fade old points so map does not fill permanently
        self.grid = (self.grid * 0.97).astype(np.uint8)

        # Resize for better visibility on the web
        grid_vis = cv2.resize(self.grid, (600, 600), interpolation=cv2.INTER_NEAREST)
        
        # Draw robot center
        h, w = grid_vis.shape[:2]
        cv2.circle(grid_vis, (w // 2, h // 2), 4, 255, -1)

        # Encode to JPEG
        ret, buffer = cv2.imencode('.jpg', grid_vis)
        return buffer.tobytes() if ret else None


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
        return cap
    raise RuntimeError(f"Failed to open camera index {cam_index}")


# -----------------------------
# Camera Tracker Class
# -----------------------------
class CameraTracker:
    def __init__(self, cam_index=None):
        cams = find_cameras()
        if not cams:
            raise RuntimeError("No cameras detected.")

        if cam_index is None:
            cam_index = cams[0]
        elif cam_index not in cams:
            print(f"[WARNING] Camera {cam_index} not found. Using {cams[0]} instead.")
            cam_index = cams[0]

        print(f"[INFO] Using camera index: {cam_index}")
        self.cap = open_camera(cam_index)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

        # Initialize the Grid Map
        self.grid_map = GridMap()

        # AprilTag Detector
        self.detector = Detector(
            families="tag36h11",
            nthreads=2,
            quad_decimate=2.0,
            quad_sigma=0.0,
            refine_edges=1,
            decode_sharpening=0.25,
            debug=0
        )

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

            # For a 2D map: Z from camera is forward (x_world), X from camera is horizontal (y_world)
            x_world = tz
            y_world = -tx
            
            # Update the global map
            self.grid_map.update(x_world, y_world)

            # Rotation (for logging)
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
            cv2.putText(frame, f"ID: {tag_id}", (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
            cv2.putText(frame, f"X:{x_world:.2f}m Y:{y_world:.2f}m", (20, 75), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)

            print(f"[DETECTED] ID: {tag_id} | Map X:{x_world:.2f} Y:{y_world:.2f}")

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