from csv import writer
from csv import reader
import argparse
import random

def get_args():
    parser = argparse.ArgumentParser(description='append binary class label to the file and extend to specific number of rows')
    parser.add_argument('--input_file', type=str, help='the input file')
    parser.add_argument('--output_file', type=str, help='the output file')

    return parser.parse_args()

args = get_args()

with open(args.input_file, 'r') as read_obj, \
        open(args.output_file, 'w', newline='') as write_obj:
    # Create a csv.reader object from the input file object
    csv_reader = reader(read_obj)
    # Create a csv.writer object from the output file object
    csv_writer = writer(write_obj)
    # Read each row of the input csv file as list
    for row in csv_reader:
        # Append the default text in the row / list
        k = random.randint(0, 1) # decide on k once
        row.append(k)
        # Add the updated row / list to the output file
        csv_writer.writerow(row)


