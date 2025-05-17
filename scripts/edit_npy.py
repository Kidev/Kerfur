import numpy as np
import argparse
import cv2
import os


def remove_last_row_and_column(input_file, output_file=None):
    """
    Removes both the last row and last column from a NumPy array stored in a .npy file

    Args:
        input_file: Path to the input .npy file
        output_file: Path to save the modified array (if None, will overwrite input file)
    """
    # Load the array
    pixel_array = np.load(input_file)

    # Display original shape
    print(f"Original array shape: {pixel_array.shape}")

    # Remove the last row and column
    modified_array = pixel_array[:-1, :-1]

    # Display new shape
    print(f"New array shape: {modified_array.shape}")

    # Determine output path
    if output_file is None:
        output_file = input_file

    # Save the modified array
    np.save(output_file, modified_array)
    print(f"Modified array saved to {output_file}")


def add_empty_columns(input_file, output_file=None):
    """
    Adds empty columns (filled with zeros) at the start and end of a NumPy array
    stored in a .npy file

    Args:
        input_file: Path to the input .npy file
        output_file: Path to save the modified array (if None, will overwrite input file)
    """
    # Load the array
    pixel_array = np.load(input_file)

    # Display original shape
    original_height, original_width = pixel_array.shape
    print(f"Original array shape: {pixel_array.shape}")

    # Create a new array with width + 2 to account for new first and last columns
    # The new array keeps the same data type as the original
    modified_array = np.zeros((original_height, original_width + 2), dtype=pixel_array.dtype)

    # Copy the original array into the middle section of the new array
    # New array: [empty_column, original_array, empty_column]
    modified_array[:, 1:-1] = pixel_array

    # Display new shape
    print(f"New array shape: {modified_array.shape}")

    # Determine output path
    if output_file is None:
        output_file = input_file

    # Save the modified array
    np.save(output_file, modified_array)
    print(f"Modified array saved to {output_file}")

def npy_to_texture(input_file, output_file=None, scale=1):
    """
    Convert a NumPy array from a .npy file to a texture PNG format suitable for the shader.

    Args:
        input_file: Path to the input .npy file
        output_file: Path to save the PNG file (if None, will use input name with .png extension)
        scale: Scale factor for the output image (default: 1)
    """
    # Load the array
    pixel_array = np.load(input_file)

    # Get dimensions
    height, width = pixel_array.shape
    print(f"Converting pixel array of shape: {pixel_array.shape}")

    # Create an image (white where pixels are 1, black elsewhere)
    image = np.zeros((height, width), dtype=np.uint8)
    image[pixel_array == 1] = 255

    # Scale if needed
    if scale > 1:
        image = cv2.resize(image, (width * scale, height * scale),
                           interpolation=cv2.INTER_NEAREST)

    # Determine output path
    if output_file is None:
        base_name = os.path.splitext(input_file)[0]
        output_file = f"{base_name}.png"

    # Save as PNG
    cv2.imwrite(output_file, image)

    # Also save metadata JSON
    metadata_file = os.path.splitext(output_file)[0] + "_meta.json"
    import json
    with open(metadata_file, 'w') as f:
        json.dump({
            "width": width,
            "height": height,
            "scale": scale
        }, f)

    print(f"Converted pixel array to texture at {output_file}")
    print(f"Metadata saved to {metadata_file}")



if __name__ == "__main__":
    #remove_last_row_and_column("npy/full_kerfur.npy", "npy/kerfur_final.npy")
    #add_empty_columns("npy/kerfur_final.npy", "npy/kerfur_final2.npy")
    npy_to_texture("npy/kerfur_final2.npy", "textures/kerfur_face.png", 1)
