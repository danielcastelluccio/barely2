import os
import subprocess
import sys

def run(compiler, directory):
    tests_count = 0
    tests_passed = 0

    index_file = open(directory + "/index")

    directory_files = os.listdir(directory)
    directory_files.remove("index")

    for file_name in index_file.readlines():
        file_name = file_name[:-1]
        directory_files.remove(file_name)
        file = open(directory + "/" + file_name)
        contents = file.readlines()

        for line in contents:
            if line.startswith("//@out: "):
                wanted_output = ""
                output_line = line[8:-1]
                i = 0
                while i < len(output_line):
                    char = output_line[i]
                    if char == '\\':
                        match output_line[i + 1]:
                            case 'n':
                                wanted_output += '\n'
                        i += 2
                    else:
                        wanted_output += char
                        i += 1


        file.close()

        passed = True

        output = subprocess.run([compiler, directory + "/" + file_name], capture_output = True, text = True)
        if output.returncode != 0:
            print("FAILED: " + file_name + " (compile)")
            print(output.stdout, end='')
            passed = False

        output = subprocess.run(["./output"], capture_output = True, text = True)
        if output.stdout[:-1] != wanted_output:
            print("FAILED: " + file_name + " (behavior)")
            print("Expected: " + str(bytes(wanted_output, 'utf-8')) + " Given: " + str(bytes(output.stdout[:-1], 'utf-8')))
            passed = False

        tests_count += 1
        if passed:
            print("SUCCESS: " + file_name)
            tests_passed += 1

    print(" - " + str(tests_passed) + "/" + str(tests_count) + " tests passed")

    if len(directory_files) > 0:
        print(" - NOTE: test files " + str(directory_files) + " not included")

args = sys.argv
assert(len(args) == 4)
compiler = args[1]
directory = args[2]
command = args[3]

match command:
    case "run":
        run(compiler, directory)
