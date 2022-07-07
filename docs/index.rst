.. Images
.. |python35_1| image:: images/python35_1.jpg

.. Links
.. |python27| raw:: html

   <a href="../fledge-filter-python27/index.html">fledge-filter-python27</a>

Python 3.5 Filter
=================

The *fledge-filter-python35* filter allows snippets of Python to be easily written that can be used as filters in Fledge. A similar filter exists that uses Python 2.7 syntax, the |python27| filter. A Python code snippet will be called with sets of asset readings as they or read or processed in a filter pipeline. The data appears in the Python code as a JSON document passed as a Python Dict type.

The user should provide a Python function whose name matches the name given to the plugin when added to the filter pipeline of the south service or north task, e.g. if you name your filter myPython then you should have a function named myPython in the code you enter. This function is send a set of readings to process and should return a set of processed readings. The returned set of readings may be empty if the filter removes all data.

A general code syntax for the function that should be provided is;

.. code-block:: python

   def myPython(readings):
       for elem in list(readings):
           ...
       return readings

Each element that is processed has a number of attributes that may be accessed

.. list-table::
    :widths: 20 50
    :header-rows: 1

    * - Attribute
      - Description
    * - asset_code
      - The name of the asset the reading data relates to.
    * - timestamp
      - The data and time Fledge first read this data
    * - user_timestamp
      - The data and time the data for the data itself, this may differ from the timestamp above
    * - readings
      - The set of readings for the asset, this is itself an object that contains a number of key/value pairs that are the data points for this reading.

In order to access an data point within the readings, for example one named *temperature*, it is a simple case of extracting the value of with *temperature* as its key.

.. code-block:: python

   def myPython(readings):
       for elem in list(readings):
           reading = elem['readings']
           temp = reading['temperature']
           ...
       return readings

It is possible to write your Python code such that it does not know the data point names in advance, in which case you are able to iterate over the names as follows;

.. code-block:: python

   def myPython(readings):
       for elem in list(readings):
           reading = elem['readings']
           for attribute in reading:
               value = reading[attribute]
               ...
       return readings

The Python script is not limited to returning the same number of readings it receives, additional readings may be added into the pipeline or readings may be removed. If the filter removes all the readings it was sent it must still return either an empty list or it may return the *None* object.
    
A second function may be provided by the Python plugin code to accept configuration from the plugin that can be used to modify the behavior of the Python code without the need to change the code. The configuration is a JSON document which is again passed as a Python Dict to the set_filter_config function in the user provided Python code. This function should be of the form

.. code-block:: python

  def set_filter_config(configuration):
      config = json.loads(configuration['config'])
      value = config['key']
      ...
      return True

Python35 filters are added in the same way as any other filters.

  - Click on the Applications add icon for your service or task.

  - Select the *python35* plugin from the list of available plugins.

  - Name your python35 filter, this should be the same name as the Python function you will provide.

  - Click *Next* and you will be presented with the following configuration page

    +--------------+
    | |python35_1| |
    +--------------+

  - Enter the configuration for your python35 filter

    - **Python script**: This is the script that will be executed. Initially you are unable to type in this area and must load your initial script from a file using the *Choose Files* button below the text area. Once a file has been chosen and loaded you are able to update the Python code in this page.

      .. note::

         Any changes made to the script in this screen will **not** be written back to the original file it was loaded from.

    - **Configuration**: You may enter a JSON document here that will be passed to the *set_filter_config* function of your Python code.

  - Enable the python35 filter and click on *Done* to activate your plugin

Example
-------

The following example uses Python to create an exponential moving average plugin. It adds a data point called *ema* to every asset. It assumes a single data point exists within the asset, but it does not assume the name of that data point. A rate can be set for the EMA using the configuration of the plugin.

.. code-block:: python

  # generate exponential moving average

  import json

  # exponential moving average rate default value: include 7% of current value
  rate = 0.07
  # latest ema value
  latest = None

  # get configuration if provided.
  # set this JSON string in configuration:
  #      {"rate":0.07}
  def set_filter_config(configuration):
      global rate
      config = json.loads(configuration['config'])
      if ('rate' in config):
          rate = config['rate']
      return True

  # Process a reading
  def doit(reading):
      global rate, latest

      for attribute in list(reading):
          if not latest:
              latest = reading[attribute]
          else:
              latest = reading[attribute] * rate + latest * (1 - rate)
          reading[b'ema'] = latest

  # process one or more readings
  def ema(readings):
      for elem in list(readings):
          doit(elem['reading'])
      return readings

Examining the content of the Python, a few things to note are;
      
  - The filter is given the name ``ema``. This name defines the default method which will be executed, namely ema().

  - The function ``ema`` is passed 1 or more readings to process. It splits these into individual readings, and calls the function ``doit`` to perform the actual work.

  - The function ``doit`` walks through each attribute in that reading, updates a global variable ``latest`` with the latest value of the ema. It then adds an *ema* attribute to the reading.

  - The function ``ema`` returns the modified readings list which then is passed to the next filter in the pipeline.

  - set_filter_config() is called whenever the user changes the JSON configuration in the plugin. This function will alter the global variable ``rate`` that is used within the function ``doit``.

Scripting Errors
----------------

If an error occurs in the Python script details will be logged to the error log and data will not flow through the pipeline to the next filter or into the storage service. Warnings raised will not cause data to cease flowing through the pipeline.

Error Messages & Warnings
#########################

Unable to obtain a reference to the asset tracker. Changes will not be tracked
    The service is unable to obtain the required reference to the asset tracker within Fledge. Data will continue to flow through the pipeline, but there will not be ant trace of the assets that have been modified by this plugin within the pipeline.

The return type of the python35 filter function should be a list of readings.
    The python script has returned an incorrect data type. The return value of the script should be a list of readings

Badly formed reading in list returned by the Python script
    One or more of the readings in the list returned by the Python script is an incorrectly formed reading object.

Each element returned by the script must be a Python DICT
    The list returned by the Python script contains an element that is not a DICT and therefore can not be a valid reading.

Badly formed reading in list returned by the Python script: Reading has no asset code element
    One or more of the readings that is returned in the list from the script is missing the *asset_code* key. This item is the name of the asset to which the reading refers.

Badly formed reading in list returned by the Python script: Reading is missing the reading element which should contain the data
    One or more of the readings that is returned in the list from the script is missing the *reading* DICT that contains the actual data.

Badly formed reading in list returned by the Python script: The reading element in the python Reading is of an incorrect type, it should be a Python DICT
    One or more of the readings that is returned in the list from the script has an item with a key of *reading* which is not a Python Dict. This item should always be a DICT and contains the data values as key/value pairs.

Badly formed reading in list returned by the Python script:  Unable to parse the asset code value. Asset codes should be a string.
    One or more of the readings that is returned in the list from the script has an item with a key of *asset_code* whose value is not a string.

