To choose the metric go in metric.h and comment/uncomment
#define USE_LQI_METRIC

The timestamp has the following format: 15:00 -> 1500, 16:47 -> 1647.
In general: HH:MM -> HHMM

To run the analysis.py script you will need Pandas, a data analysis library for Python.

Then install pandas using pip:
$ pip install pandas

# Cooja
By default, you should just run:
$ <path_to_scripts>/parser.py test.log --cooja
$ <path_to_scripts>/analysis.py . --cooja

# Testbed (UNITN Cloves)
To get the PDR:
- First download and extract the job archive
- Then run (assuming you are in the extracted folder)
   $ <path_to_scripts>/parser.py job.log --testbed
   $ <path_to_scripts>/analysis.py . --testbed

To get the duty cycle run
$ <path_to_scripts>/energest-stats.py job.log --testbed


