"""
Fledge filtering for readings data
using Python 3.5

This filter scales each datapoint value in input readings data
"""

__author__ = "Massimiliano Pinto"
__copyright__ = "Copyright (c) 2019 Dianomic Systems"
__license__ = "Apache 2.0"
__version__ = "${VERSION}"

import json

# Filter scripts run in a restricted environment: os, sys, subprocess, socket,
# logging and similar modules can not be imported, so diagnostics go to stdout.


class _Logger(object):
    def debug(self, msg):
        print("DEBUG: " + msg)


logger = _Logger()

"""
Filter configuration set by set_filter_config(config)
"""

"""
filter_config, global variable
"""
filter_config = dict()

"""
Set the Filter configuration into filter_config (global variable)
Input data is a dict with 'config' key and a dict of configuration values
JSON string is loaded into a dict, set to global variable filter_config

This method is called:
 - when the plugin is loaded with a valid Python script
 - when the filter configuration is changed by the user

Configuration example of 'config' item:

  "config": {
    "displayName": "Configuration",
    "value": "{\"offset\":5,\"scale\":2}",
    "order": "1",
    "type": "JSON",
    "description": "Python 3.5 filter configuration.",
    "default": "{}"
  }

The method argument is {"config": {"offset":5, "scale":2}}

Arguments:
configuration -- The JSON configuration

Returns:
True
"""
def set_filter_config(configuration):
    logger.debug("Config = " + str(configuration)) 
    global filter_config
    filter_config = json.loads(configuration['config'])

    return True

"""
Method for filtering readings data

This method is called whenever readings data is available:
the input data is processed and new data is returned
Input is array of dicts, example
[
    {'reading': {'power_set1': '5'}, 'asset_code': 'lab1'},
    {'reading': {'power_set1': '10'}, 'asset_code': 'lab1'}
]

Output example (using scale = 5 and offset = 10):
[
    {'reading': {'power_set1': '35'}, 'asset_code': 'lab1'},
    {'reading': {'power_set1': '60'}, 'asset_code': 'lab1'}
]

Note: the method name must be the same as script name.

Arguments:
readings -- An array of dicts

Returns:
An array of dicts with modified or dropped readings data
"""
def scale35(readings):
    # Default config values
    scale = 5
    offset = 10

    # Get configuration, if available
    if ('scale' in filter_config):
        scale = filter_config['scale']
    if ('offset' in filter_config):
        offset = filter_config['offset']

    # Process input data
    for elem in readings:
            logger.debug("IN=" + str(elem))
            reading = elem['reading']

            # Apply some changes: multiply datapoint values by scale and add offset
            for key in reading:
                newVal = reading[key] * scale + offset
                reading[key] = newVal

            logger.debug("OUT=" + str(elem))
    return readings
