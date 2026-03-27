import cv2
import numpy as np
from pupil_apriltags import Detector
import math
import platform
import threading
import time
import traceback

# -----------------------------
# Grid Map Class
# -----------------------------
class GridMap:
    def __init__(self, size=200, resolution=0.05):
        self.size = size
        self.resolution = resolution
        self.center = size // 2
        self.grid = np.zeros((size, size), dtype=np.uint8)

    def update(self, x, y, strength=50):
        """Convert world coordinates (meters) to grid indices and update."""
        gx = int(x / self.resolution) + self.center
        gy = int(y / self.resolution) + self.center

        if 0 <= gx < self.size and 0 <= gy < self.size:
            self.grid[gy, gx] = min(self.grid[gy, gx] + strength, 255)

    def get_map_stream(self):
        """Fades the map slightly, draws the robot, and encodes as JPEG."""
        self.grid = (self.grid * 0.97).astype(np.uint8)
        grid_vis = cv2.resize(self.grid, (600, 600), interpolation=cv2.INTER_NEAREST)
        
        # Draw robot center
        h, w = grid_vis.shape[:2]
        cv2.circle(grid_vis, (w // 2, h // 2), 4, 255, -1)

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
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        return cap

    raise RuntimeError(f"Failed to open camera index {cam_index}")


# -----------------------------
# Camera Tracker Class
# -----------------------------
class CameraTracker:
    def __init__(self, cam_index=0):
        self.grid_map = GridMap()
        self.cap = open_camera(cam_index)

        # AprilTag Detector
        self.detector = Detector(
            families="tag36h11", nthreads=2, quad_decimate=2.0,
            quad_sigma=0.0, refine_edges=1, decode_sharpening=0.25, debug=0
        )
        self.tag_size_m = 0.152
        self.camera_params = (900.0, 900.0, 1280 / 2.0, 720 / 2.0)

        # Threading state
        self.running = True
        self.latest_frame = None

        # Start background processing thread
        self.thread = threading.Thread(target=self.update_loop, daemon=True)
        self.thread.start()
        print("[INFO] Camera background thread started.")

    def process_frame(self, frame):
        """Detects ALL tags in a frame and maps them."""
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        tags = self.detector.detect(
            gray, estimate_tag_pose=True, camera_params=self.camera_params, tag_size=self.tag_size_m
        )

        for tag in tags:
            # Safety check: Sometimes a tag is seen but pose estimation fails
            if tag.pose_t is not None:
                tx, ty, tz = tag.pose_t.flatten().tolist()
                
                # Camera frame to local 2D frame (Map coordinates)
                x_world = tz
                y_world = -tx

                # Update Map
                self.grid_map.update(x_world, y_world)

                # Draw green box for successful pose
                corners = tag.corners.astype(int)
                for i in range(4):
                    cv2.line(frame, tuple(corners[i]), tuple(corners[(i + 1) % 4]), (0, 255, 0), 2)
                
                # Setup coordinates for text placement
                center = tuple(tag.center.astype(int))
                
                # 1. Overlay Tag ID (Green text)
                cv2.putText(frame, f"ID: {tag.tag_id}", (center[0]-30, center[1]-30), 
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
                
                # 2. Overlay Live X/Y Data (Yellow text)
                cv2.putText(frame, f"X:{x_world:.2f}m Y:{y_world:.2f}m", (center[0]-30, center[1]-5), 
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)
                
                # 3. Print a clean, formatted read-out to the terminal
                print(f"[LIVE] Tag ID {tag.tag_id:02d} | Map X: {x_world:+.2f}m | Map Y: {y_world:+.2f}m")

            else:
                # Draw red box if pose estimation failed on this frame (too blurry)
                corners = tag.corners.astype(int)
                for i in range(4):
                    cv2.line(frame, tuple(corners[i]), tuple(corners[(i + 1) % 4]), (0, 0, 255), 2)

        # Encode frame
        ret, buffer = cv2.imencode('.jpg', frame)
        return buffer.tobytes() if ret else None

    def update_loop(self):
        """Continuously reads frames with crash-proof error handling."""
        while self.running:
            try:
                ok, frame = self.cap.read()
                if ok and frame is not None:
                    self.latest_frame = self.process_frame(frame)
                else:
                    print("[WARNING] Frame dropped or camera disconnected. Retrying...")
                    time.sleep(0.5)
            except Exception as e:
                print(f"[ERROR in Camera Thread]: {e}")
                traceback.print_exc()
                time.sleep(1)

    def release(self):
        self.running = False
        self.thread.join(timeout=1.0)
        if self.cap:
            self.cap.release()
            print("[INFO] Camera released.")