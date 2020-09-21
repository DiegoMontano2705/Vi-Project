# Vi-Project = Text Editor in C
Text editor created in C based on the /kilo (https://viewsourcecode.org/snaptoken/kilo/ ) text editor
Made by Arturo Baez, Diego Montaño and Monica Nava

Cygwin64 is required to run the code in Windows.

# How to run the code
In terminal:
```bash
$ main.c -o main
$ ./main [file.txt]
```
# Characteristics of the Vi
```C
:q -  exit Ask user if they want to save changes before quitting
:wq - quit automatically saving changes
:n - go to line number
:f text - Number of occurrences in text
:s text - Navigate to text 
Disabled CTRL+Z and CTRL+C to quit program
```
#Bugs
Enter isnt working properly when editing text
