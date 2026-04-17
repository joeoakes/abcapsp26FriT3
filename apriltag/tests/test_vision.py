import pytest
import cv2
import os

# Import your actual detection function here. 
# (Adjust 'vision_pipeline' to match your actual Python file name)
# from vision_pipeline import detect_tag 

# --- MOCK FUNCTION FOR DEMONSTRATION ---
# Remove this block and use the import above once your real code is linked.
def detect_tag(image):
    # If the image is completely black or empty, return no tags (TC-062)
    if image is None or image.mean() < 10:
        return []
    
    # Otherwise, return a mock valid detection (TC-061, TC-068)
    return [{
        "tag_id": 3,
        "pose_x": 0.15,
        "pose_y": -0.05,
        "pose_z": 1.20,
        "center": (320, 240)
    }]
# ---------------------------------------

# Helper to get the absolute path to your test images
FIXTURE_DIR = os.path.join(os.path.dirname(__file__), 'fixtures')

class TestAprilTagVision:

    @pytest.fixture
    def valid_tag_image(self):
        """Loads a test image containing a clear AprilTag."""
        img_path = os.path.join(FIXTURE_DIR, 'tag_valid.jpg')
        image = cv2.imread(img_path)
        
        # Fallback if you haven't taken the photo yet so the test doesn't crash
        if image is None:
            import numpy as np
            image = np.ones((480, 640, 3), dtype=np.uint8) * 200 # Gray image
            
        return image

    @pytest.fixture
    def empty_tag_image(self):
        """Loads a test image of a blank wall with no AprilTags."""
        img_path = os.path.join(FIXTURE_DIR, 'tag_empty.jpg')
        image = cv2.imread(img_path)
        
        # Fallback: a purely black image
        if image is None:
            import numpy as np
            image = np.zeros((480, 640, 3), dtype=np.uint8)
            
        return image

    # ==========================================
    # ACTUAL TEST CASES (From your spreadsheet)
    # ==========================================

    def test_tc_061_detect_valid_tag(self, valid_tag_image):
        """
        TC-061: Verify AprilTag detection function returns tag ID 
        and pose data for a clear test image.
        """
        # Act: Pass the image to your pipeline
        detections = detect_tag(valid_tag_image)

        # Assert: Check that we got exactly what the test plan expects
        assert isinstance(detections, list), "Detection should return a list."
        assert len(detections) > 0, "Failed to detect the tag in a clear image."
        
        first_tag = detections[0]
        
        # Verify the required fields are present
        assert "tag_id" in first_tag, "Detection missing 'tag_id' field."
        assert "pose_x" in first_tag, "Detection missing pose X data."
        assert "pose_z" in first_tag, "Detection missing pose Z (depth) data."
        
        # Ensure pose data is a valid number (float or int)
        assert isinstance(first_tag["pose_z"], (float, int)), "Pose Z must be numeric."

    def test_tc_062_no_false_positives(self, empty_tag_image):
        """
        TC-062: Verify no false-positive tag is returned for 
        an image without AprilTags.
        """
        # Act: Run detection on the empty wall image
        detections = detect_tag(empty_tag_image)

        # Assert: The list must be completely empty
        assert len(detections) == 0, f"False positive detected! Found: {detections}"

    def test_tc_068_regression_id_parsing(self, valid_tag_image):
        """
        TC-068: Verify camera-reading updates do not break AprilTag ID parsing.
        This ensures that even if we change the pose math, ID '3' is still read as '3'.
        """
        # Note: You should know exactly what ID is in 'tag_valid.jpg'
        EXPECTED_TAG_ID = 3 
        
        # Act
        detections = detect_tag(valid_tag_image)
        
        # Assert
        assert len(detections) > 0, "Tag not found during regression test."
        assert detections[0]["tag_id"] == EXPECTED_TAG_ID, \
            f"Regression Failure: Expected ID {EXPECTED_TAG_ID}, got {detections[0]['tag_id']}"