import numpy as np
import argparse


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


if __name__ == "__main__":
    #remove_last_row_and_column("full_kerfur.npy", "kerfur_final.npy")
    add_empty_columns("kerfur_final.npy", "kerfur_final2.npy")
