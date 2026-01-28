# Testing
To run the analysis.py script you will need Pandas, a data analysis library for Python.

If you are using the virtual machine first install pip:
 - This will download a get-pip.py file in the folder in which you are currently in
	$ wget https://bootstrap.pypa.io/pip/3.7/get-pip.py
 - This will install pip (package manager for python)
	$ python3.7 get-pip.py

Then install pandas using pip:
$ pip install pandas

# Cooja
By default, you should just run:
$ <path_to_scripts>/parser.py test.log --cooja
$ <path_to_scripts>/analysis.py . --cooja

# Testbed
To get the PDR:
- First download and extract the job archive
- Then run (assuming you are in the extracted folder)
   $ <path_to_scripts>/parser.py job.log --testbed
   $ <path_to_scripts>/analysis.py . --testbed

To get the duty cycle run
$ <path_to_scripts>/energest-stats.py job.log --testbed

