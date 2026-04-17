import pytest
import cv2
import os

# --- MOCK SETUP: Replace these with your actual module imports ---
# from vision_pipeline import detect_tag
# from telemetry_client import send_tag_to_server 

def detect_tag(image):
    """Mock detection: returns a tag if image is valid."""
    if image is None or image.mean() < 10:
        return []
    return [{"tag_id": 3, "pose_z": 1.20}]

# Mock downstream consumer class to act as our ROS2 node or Server
class MockDownstreamConsumer:
    def __init__(self):
        self.received_messages = []
        
    def send_tag_data(self, tag_data):
        """Simulates receiving data over ROS2 or HTTP."""
        self.received_messages.append(tag_data)
# -----------------------------------------------------------------

FIXTURE_DIR = os.path.join(os.path.dirname(__file__), 'fixtures')

class TestSystemIntegration:

    @pytest.fixture
    def valid_tag_image(self):
        img_path = os.path.join(FIXTURE_DIR, 'tag_valid.jpg')
        image = cv2.imread(img_path)
        if image is None:
            import numpy as np
            image = np.ones((480, 640, 3), dtype=np.uint8) * 200
        return image

    def test_tc_064_downstream_handoff(self, valid_tag_image):
        """
        TC-064: Verify detected tag readings are passed to the correct downstream module.
        Environment: Integration / Grey Box
        """
        # Arrange: Setup our mock downstream module (Maze App / Telemetry)
        downstream_module = MockDownstreamConsumer()
        
        # Act Step 1: Vision system processes the frame
        detections = detect_tag(valid_tag_image)
        
        # Act Step 2: Pipeline logic that sends data downstream
        if detections:
            for tag in detections:
                downstream_module.send_tag_data(tag)
                
        # Assert: Did the downstream module actually receive the payload?
        assert len(downstream_module.received_messages) > 0, "Downstream module did not receive any data."
        
        # Verify the integrity of the data that was handed off
        received_payload = downstream_module.received_messages[0]
        assert received_payload["tag_id"] == 3, "Downstream module received corrupted or incorrect Tag ID."
        
        print("\n✅ Handoff Successful: Downstream module received payload:", received_payload)