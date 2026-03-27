import cv2
import numpy as np
from pupil_apriltags import Detector
import math
import platform

# =========================
# Grid settings
# =========================
GRID_SIZE = 200            # 200 x 200 cells
GRID_RESOLUTION = 0.05     # each cell = 5 cm
GRID_CENTER = GRID_SIZE // 2

grid = np.zeros((GRID_SIZE, GRID_SIZE), dtype=np.uint8)


def world_to_grid(x, y):
    """
    Convert world coordinates in meters to grid indices.
    x = forward/back
    y = left/right
    """
    gx = int(x / GRID_RESOLUTION) + GRID_CENTER
    gy = int(y / GRID_RESOLUTION) + GRID_CENTER
    return gx, gy


def update_grid(x, y, strength=25):
    """
    Add a point to the occupancy grid.
    """
    gx, gy = world_to_grid(x, y)

    if 0 <= gx < GRID_SIZE and 0 <= gy < GRID_SIZE:
        grid[gy, gx] = min(grid[gy, gx] + strength, 255)


def draw_robot_on_grid(grid_img):
    """
    Draw robot at the center of the displayed map.
    """
    h, w = grid_img.shape[:2]
    center_x = w // 2
    center_y = h // 2
    cv2.circle(grid_img, (center_x, center_y), 5, 255, -1)


def open_camera(index):
    """
    Cross-platform camera open.
    Linux   -> V4L2
    Windows -> DirectShow
    Other   -> default backend
    """
    os_name = platform.system()

    if os_name == "Linux":
        return cv2.VideoCapture(index, cv2.CAP_V4L2)
    elif os_name == "Windows":
        return cv2.VideoCapture(index, cv2.CAP_DSHOW)
    else:
        return cv2.VideoCapture(index)


def process_frame(frame, detector, fx, fy, cx, cy, tag_size_m, cam_name="center", rotation_deg=0):
    """
    Detect AprilTags in one frame and update the global grid.

    rotation_deg:
        0   = camera faces forward
        90  = camera faces right
        -90 = camera faces left
        180 = camera faces backward
    """
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    tags = detector.detect(
        gray,
        estimate_tag_pose=True,
        camera_params=(fx, fy, cx, cy),
        tag_size=tag_size_m
    )

    if not tags:
        return frame

    # Choose strongest detection
    best = max(tags, key=lambda t: t.decision_margin)

    tx, ty, tz = best.pose_t.flatten().tolist()

    # Camera coordinates
    # tz = forward distance
    # tx = horizontal offset
    # ty = vertical offset (ignored for 2D map)
    x_cam = tz
    y_cam = -tx

    # Rotate into robot/world frame depending on camera mounting
    if rotation_deg == 0:
        x_world, y_world = x_cam, y_cam
    elif rotation_deg == 90:
        x_world, y_world = y_cam, -x_cam
    elif rotation_deg == -90:
        x_world, y_world = -y_cam, x_cam
    elif rotation_deg == 180:
        x_world, y_world = -x_cam, -y_cam
    else:
        theta = math.radians(rotation_deg)
        x_world = x_cam * math.cos(theta) - y_cam * math.sin(theta)
        y_world = x_cam * math.sin(theta) + y_cam * math.cos(theta)

    update_grid(x_world, y_world)

    # Draw tag outline
    corners = best.corners.astype(int)
    for i in range(4):
        pt1 = tuple(corners[i])
        pt2 = tuple(corners[(i + 1) % 4])
        cv2.line(frame, pt1, pt2, (0, 255, 0), 2)

    center = tuple(best.center.astype(int))
    cv2.circle(frame, center, 5, (0, 0, 255), -1)

    # Orientation
    R = best.pose_R
    yaw = math.atan2(R[1, 0], R[0, 0])
    pitch = math.atan2(-R[2, 0], math.sqrt(R[2, 1] ** 2 + R[2, 2] ** 2))
    roll = math.atan2(R[2, 1], R[2, 2])
    roll, pitch, yaw = map(math.degrees, (roll, pitch, yaw))

    # Overlay text
    cv2.putText(frame, f"{cam_name} cam", (20, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 0), 2)
    cv2.putText(frame, f"ID: {best.tag_id}", (20, 60),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
    cv2.putText(frame, f"X:{x_world:.2f} Y:{y_world:.2f}", (20, 90),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
    cv2.putText(frame, f"Yaw:{yaw:.1f}", (20, 120),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)

    print(
        f"[{cam_name}] ID: {best.tag_id} | "
        f"world x={x_world:.2f}m, y={y_world:.2f}m | "
        f"roll={roll:.0f}, pitch={pitch:.0f}, yaw={yaw:.0f}"
    )

    return frame


def main():
    # =========================
    # Camera setup
    # =========================
    # Center camera = Mini Pupper camera
    # Side camera   = external webcam
    center_cam_index = 0
    side_cam_index = 1

    cap_center = open_camera(center_cam_index)
    cap_side = open_camera(side_cam_index)

    # Set resolution if cameras are available
    if cap_center is not None and cap_center.isOpened():
        cap_center.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        cap_center.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    if cap_side is not None and cap_side.isOpened():
        cap_side.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        cap_side.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    if not cap_center.isOpened():
        raise RuntimeError("Could not open center camera (Mini Pupper / primary webcam).")

    if not cap_side.isOpened():
        print("Warning: side camera (webcam) not found. Running with center camera only.")
        cap_side.release()
        cap_side = None

    # =========================
    # AprilTag detector
    # =========================
    detector = Detector(
        families="tag36h11",
        nthreads=2,
        quad_decimate=2.0,
        quad_sigma=0.0,
        refine_edges=1,
        decode_sharpening=0.25,
        debug=0
    )

    # =========================
    # Camera intrinsics
    # =========================
    tag_size_m = 0.152

    w, h = 1280, 720
    fx = fy = 900.0
    cx = w / 2.0
    cy = h / 2.0

    print("AprilTag grid mapping started.")
    print("Center camera = Mini Pupper camera / primary webcam")
    print("Side camera   = external webcam")
    print("Press Q to quit.")
    print("Press C to clear the map.")

    try:
        while True:
            ok1, frame_center = cap_center.read()
            if not ok1:
                print("Failed to read center camera frame.")
                break

            ok2 = False
            frame_side = None
            if cap_side is not None:
                ok2, frame_side = cap_side.read()
                if not ok2:
                    print("Warning: failed to read side camera frame.")

            # Fade old points so map doesn't fill forever
            global grid
            grid = (grid * 0.97).astype(np.uint8)

            # Process center camera
            frame_center = process_frame(
                frame_center,
                detector,
                fx, fy, cx, cy,
                tag_size_m,
                cam_name="center",
                rotation_deg=0
            )

            # Process side camera
            # Change rotation_deg to -90 if your external webcam faces left instead of right
            if cap_side is not None and ok2 and frame_side is not None:
                frame_side = process_frame(
                    frame_side,
                    detector,
                    fx, fy, cx, cy,
                    tag_size_m,
                    cam_name="side",
                    rotation_deg=90
                )

            # Build map display
            grid_vis = cv2.resize(grid, (600, 600), interpolation=cv2.INTER_NEAREST)
            draw_robot_on_grid(grid_vis)

            cv2.imshow("Center Camera", frame_center)

            if cap_side is not None and frame_side is not None:
                cv2.imshow("Side Camera", frame_side)

            cv2.imshow("2D Grid Map", grid_vis)

            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                break
            elif key == ord('c'):
                grid[:] = 0
                print("Grid cleared.")

    except KeyboardInterrupt:
        print("\nStopped by user.")

    finally:
        if cap_center is not None:
            cap_center.release()
        if cap_side is not None:
            cap_side.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()