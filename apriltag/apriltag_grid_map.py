import cv2
import numpy as np
from pupil_apriltags import Detector
import math

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
    Draw robot at the center of the map.
    """
    h, w = grid_img.shape[:2]
    center_x = w // 2
    center_y = h // 2
    cv2.circle(grid_img, (center_x, center_y), 4, 255, -1)

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

    # choose best detection
    best = max(tags, key=lambda t: t.decision_margin)

    tx, ty, tz = best.pose_t.flatten().tolist()

    # AprilTag pose meaning:
    # tz = forward distance from camera
    # tx = horizontal offset from camera
    # ty = vertical offset
    #
    # For our 2D map:
    # x = forward
    # y = sideways
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
        # fallback if you later want custom angles
        theta = math.radians(rotation_deg)
        x_world = x_cam * math.cos(theta) - y_cam * math.sin(theta)
        y_world = x_cam * math.sin(theta) + y_cam * math.cos(theta)

    update_grid(x_world, y_world)

    # draw box around tag on camera frame
    corners = best.corners.astype(int)
    for i in range(4):
        pt1 = tuple(corners[i])
        pt2 = tuple(corners[(i + 1) % 4])
        cv2.line(frame, pt1, pt2, (0, 255, 0), 2)

    center = tuple(best.center.astype(int))
    cv2.circle(frame, center, 5, (0, 0, 255), -1)

    # orientation
    R = best.pose_R
    yaw = math.atan2(R[1, 0], R[0, 0])
    pitch = math.atan2(-R[2, 0], math.sqrt(R[2, 1]**2 + R[2, 2]**2))
    roll = math.atan2(R[2, 1], R[2, 2])
    roll, pitch, yaw = map(math.degrees, (roll, pitch, yaw))

    # text for camera window
    cv2.putText(frame, f"{cam_name} cam", (20, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 0), 2)
    cv2.putText(frame, f"ID: {best.tag_id}", (20, 60),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
    cv2.putText(frame, f"X:{x_world:.2f} Y:{y_world:.2f}", (20, 90),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
    cv2.putText(frame, f"Yaw:{yaw:.1f}", (20, 120),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)

    print(f"[{cam_name}] ID: {best.tag_id} | world x={x_world:.2f}m, y={y_world:.2f}m | roll={roll:.0f}, pitch={pitch:.0f}, yaw={yaw:.0f}")

    return frame

def main():
    # =========================
    # Camera setup
    # =========================
    center_cam_index = 0
    side_cam_index = 1

    cap_center = cv2.VideoCapture(center_cam_index, cv2.CAP_V4L2)
    cap_side = cv2.VideoCapture(side_cam_index, cv2.CAP_V4L2)

    for cap in [cap_center, cap_side]:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    if not cap_center.isOpened():
        raise RuntimeError("Could not open center camera.")
    if not cap_side.isOpened():
        raise RuntimeError("Could not open side camera.")

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

    print("Two-camera AprilTag grid mapping started.")
    print("Press Q to quit.")
    print("Press C to clear the map.")

    try:
        while True:
            ok1, frame_center = cap_center.read()
            ok2, frame_side = cap_side.read()

            if not ok1:
                print("Failed to read center camera frame.")
                break
            if not ok2:
                print("Failed to read side camera frame.")
                break

            # Slowly fade old points so map does not fill permanently
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
            # Change rotation_deg depending on how the side camera is mounted:
            # 90  = facing right
            # -90 = facing left
            frame_side = process_frame(
                frame_side,
                detector,
                fx, fy, cx, cy,
                tag_size_m,
                cam_name="side",
                rotation_deg=90
            )

            # Make map display larger
            grid_vis = cv2.resize(grid, (600, 600), interpolation=cv2.INTER_NEAREST)
            draw_robot_on_grid(grid_vis)

            cv2.imshow("Center Camera", frame_center)
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
        cap_center.release()
        cap_side.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
