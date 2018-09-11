/*
 * FogLAMP "Python 3.r57" filter plugin.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <plugin_api.h>
#include <config_category.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include <iostream>
#include <filter_plugin.h>
#include <filter.h>
#include <reading_set.h>
#include <utils.h>

#include <Python.h>

// Relative path to FOGLAMP_DATA
#define PYTHON_FILTERS_PATH "/filters"
#define FILTER_NAME "python35"

/**
 * The Python 3.5 script module to load is set in
 * 'script' config item and it doesn't need the trailing .py
 *
 * Example:
 * if filename is 'readings_filter.py', just set 'readings_filter'
 * via FogLAMP configuration managewr
 *
 * Note:
 * Python 3.5 filter code needs two methods.
 *
 * One is the filtering method to call which must have
 * the same as the script name: it can not be changed.
 * The second one is the configuration entry point
 * method 'set_filter_config': it can not be changed
 *
 * Example: readings_filter.py
 *
 * expected two methods:
 * - set_filter_config(configuration) // Input is a string
 *   It sets the configuration internally as dict
 *
 * - readings_filter(readings) // Input is a dict
 *   It returns a dict with filtered input data
 */

// Filter configuration method
#define DEFAULT_FILTER_CONFIG_METHOD "set_filter_config"
// Filter default configuration
#define DEFAULT_CONFIG "{\"plugin\" : { \"description\" : \"Python 3.5 filter plugin\", " \
                       		"\"type\" : \"string\", " \
				"\"default\" : \"" FILTER_NAME "\" }, " \
			 "\"enable\": {\"description\": \"A switch that can be used to enable or disable execution of " \
					 "the Python 3.5 filter.\", " \
				"\"type\": \"boolean\", " \
				"\"default\": \"false\" }, " \
			"\"config\" : {\"description\" : \"Python 3.5 filter configuration.\", " \
				"\"type\" : \"JSON\", " \
				"\"default\" : {}}, " \
			"\"script\" : {\"description\" : \"Python 3.5 module to load.\", " \
				"\"type\": \"string\", " \
				"\"default\": \"""\"} }"
using namespace std;

// Python 3.5 loaded filter module handle
static PyObject* pModule = NULL;
// Python 3.5 callable method handle
static PyObject* pFunc = NULL;
// Python 3.5  script name
static string pythonScript;

// Filtering methods
static PyObject* createReadingsList(const vector<Reading *>& readings);
static vector<Reading *>* getFilteredReadings(PyObject* filteredData);
static void logErrorMessage();

/**
 * The Filter plugin interface
 */
extern "C" {
/**
 * The plugin information structure
 */
static PLUGIN_INFORMATION info = {
        FILTER_NAME,              // Name
        "1.0.0",                  // Version
        0,                        // Flags
        PLUGIN_TYPE_FILTER,       // Type
        "1.0.0",                  // Interface version
	DEFAULT_CONFIG	          // Default plugin configuration
};

/**
 * Return the information about this plugin
 */
PLUGIN_INFORMATION *plugin_info()
{
	return &info;
}

/**
 * Initialise the plugin, called to get the plugin handle and setup the
 * output handle that will be passed to the output stream. The output stream
 * is merely a function pointer that is called with the output handle and
 * the new set of readings generated by the plugin.
 *     (*output)(outHandle, readings);
 * Note that the plugin may not call the output stream if the result of
 * the filtering is that no readings are to be sent onwards in the chain.
 * This allows the plugin to discard data or to buffer it for aggregation
 * with data that follows in subsequent calls
 *
 * @param config	The configuration category for the filter
 * @param outHandle	A handle that will be passed to the output stream
 * @param output	The output stream (function pointer) to which data is passed
 * @return		An opaque handle that is used in all subsequent calls to the plugin
 */
PLUGIN_HANDLE plugin_init(ConfigCategory* config,
			  OUTPUT_HANDLE *outHandle,
			  OUTPUT_STREAM output)
{
	FogLampFilter* handle = new FogLampFilter(FILTER_NAME,
						  *config,
						  outHandle,
						  output);

	// Check whether we have a Python 3.5 script file to import
	if (handle->getConfig().itemExists("script"))
	{
		pythonScript = handle->getConfig().getValue("script");
	}
	else
	{
		pythonScript = string("");
	}
	if (pythonScript.empty())
	{
		// Do nothing
		Logger::getLogger()->warn("Filter '%s', "
					  "called without a Python 3.5 script. "
					  "Check 'script' item in '%s' configuration. "
					  "Filter has been disabled.",
					  FILTER_NAME,
					  handle->getConfig().getName().c_str());

		// Force disable
		handle->disableFilter();

		// Return filter handle
		return (PLUGIN_HANDLE)handle;
	}
		
	// Embedded Python 3.5 program name
	wchar_t *programName = Py_DecodeLocale(config->getName().c_str(), NULL);
        Py_SetProgramName(programName);
	PyMem_RawFree(programName);
	// Embedded Python 3.5 initialisation
        Py_Initialize();

	// Get FogLAMP Data dir
	string filtersPath = getDataDir();
	// Add filters dir
	filtersPath += PYTHON_FILTERS_PATH;

	// Set Python path for embedded Python 3.5
	// Get current sys.path. borrowed reference
	PyObject* sysPath = PySys_GetObject((char *)string("path").c_str());
	// Add FogLAMP python filters path
	PyObject* pPath = PyBytes_FromString((char *)filtersPath.c_str());
	PyList_Insert(sysPath, 0, pPath);
	// Remove temp object
	Py_CLEAR(pPath);

	// Set scrip tname
	PyObject* pName = PyBytes_FromString(pythonScript.c_str());

	// Import script as module
	pModule = PyImport_Import(pName);

	// Delete pName reference
	Py_CLEAR(pName);

	// Check whether the Python module has been imported
	if (!pModule)
	{
		// Failure
		if (PyErr_Occurred())
		{
			logErrorMessage();
		}
		Logger::getLogger()->fatal("Filter '%s' (%s), cannot import Python 3.5 script "
					   "'%s.py' from '%s'",
					   FILTER_NAME,
					   handle->getConfig().getName().c_str(),
					   pythonScript.c_str(),
					   filtersPath.c_str());

		// This will abort the filter pipeline set up
		return NULL;
	}

	// NOTE:
	// Filter method to call is the same as filter name
	// Fetch filter method in loaded object
	pFunc = PyObject_GetAttrString(pModule, pythonScript.c_str());

	if (!PyCallable_Check(pFunc))
        {
		// Failure
		if (PyErr_Occurred())
		{
			logErrorMessage();
		}

		Logger::getLogger()->fatal("Filter %s (%s) error: cannot find Python 3.5 method "
					   "'%s' in loaded module '%s.py'",
					   FILTER_NAME,
					   handle->getConfig().getName().c_str(),
					   pythonScript.c_str(),
					   pythonScript.c_str());
		Py_CLEAR(pModule);
		Py_CLEAR(pFunc);

		// This will abort the filter pipeline set up
		return NULL;
	}

	/**
	 * We now pass the filter JSON configuration to the loaded module
	 */
	// Set configuration object	
	PyObject* pConfig = PyDict_New();
	// Whole configuration as it is
	string filterConfiguration;

	// Get 'config' filter category configuration
	if (handle->getConfig().itemExists("config"))
	{
		filterConfiguration = handle->getConfig().getValue("config");
	}
	else
	{
		// Set empty object
		filterConfiguration = "{}";
	}

	// Add JSON configuration, as string, to "config" key
	PyObject* pConfigObject = PyBytes_FromString(filterConfiguration.c_str());
	PyDict_SetItemString(pConfig,
			     "config",
			     pConfigObject);
	Py_CLEAR(pConfigObject);

	/**
	 * Call method set_filter_config(c)
	 * This creates a global JSON configuration
	 * which will be available when fitering data with "plugin_ingest"
	 *
	 * set_filter_config(config) returns 'True'
	 */
	PyObject* pSetConfig = PyObject_CallMethod(pModule,
						   (char *)string(DEFAULT_FILTER_CONFIG_METHOD).c_str(),
						   (char *)string("O").c_str(),
						   pConfig);
	// Check result
	if (!pSetConfig ||
	    !PyBool_Check(pSetConfig) ||
	    !PyLong_AsLong(pSetConfig))
	{
		logErrorMessage();

		Py_CLEAR(pModule);
		Py_CLEAR(pFunc);
		// Remove temp objects
		Py_CLEAR(pConfig);
		Py_CLEAR(pSetConfig);

		return NULL;
	}

	// Remove temp objects
	Py_CLEAR(pSetConfig);
	Py_CLEAR(pConfig);

	// Return filter handle
	return (PLUGIN_HANDLE)handle;
}

/**
 * Ingest a set of readings into the plugin for processing
 *
 * NOTE: in case of any error, the input readings will be passed
 * onwards (untouched)
 *
 * @param handle	The plugin handle returned from plugin_init
 * @param readingSet	The readings to process
 */
void plugin_ingest(PLUGIN_HANDLE *handle,
		   READINGSET *readingSet)
{
	FogLampFilter* filter = (FogLampFilter *)handle;

	if (!filter->isEnabled())
	{
		// Current filter is not active: just pass the readings set
		filter->m_func(filter->m_data, readingSet);
		return;
	}

        // Get all the readings in the readingset
	const vector<Reading *>& readings = ((ReadingSet *)readingSet)->getAllReadings();

	/**
	 * 1 - create a Python object (list of dicts) from input data
	 * 2 - pass Python object to Python filter method
	 * 3 - Transform results from fealter into new ReadingSet
	 * 4 - Remove old data and pass new data set onwards
	 */

	// - 1 - Create Python list of dicts as input to the filter
	PyObject* readingsList = createReadingsList(readings);

	// Check for errors
	if (!readingsList)
	{
		// Errors while creating Python 3.5 filter input object
		Logger::getLogger()->error("Filter '%s' (%s), script '%s', "
					   "create filter data error, action: %s",
					   FILTER_NAME,
					   filter->getConfig().getName().c_str(),
					   pythonScript.c_str(),
					  "pass unfiltered data onwards");

		// Pass data set to next filter and return
		filter->m_func(filter->m_data, readingSet);
		return;
	}

	// - 2 - Call Python method passing an object
	PyObject* pReturn = PyObject_CallFunction(pFunc,
						  (char *)string("O").c_str(),
						  readingsList);

	// Free filter input data
	Py_CLEAR(readingsList);

	ReadingSet* finalData = NULL;

	// - 3 - Handle filter returned data
	if (!pReturn)
	{
		// Errors while getting result object
		Logger::getLogger()->error("Filter '%s' (%s), script '%s', "
					   "filter error, action: %s",
					   FILTER_NAME,
					   filter->getConfig().getName().c_str(),
					   pythonScript.c_str(),
					   "pass unfiltered data onwards");

		// Errors while getting result object
		logErrorMessage();

		// Filter did nothing: just pass input data
		finalData = (ReadingSet *)readingSet;
	}
	else
	{
		// Get new set of readings from Python filter
		vector<Reading *>* newReadings = getFilteredReadings(pReturn);
		if (newReadings)
		{
			// Filter success
			// - Delete input data as we have a new set
			delete (ReadingSet *)readingSet;
			readingSet = NULL;

			// - Set new readings with filtered/modified data
			finalData = new ReadingSet(newReadings);

			// - Remove newReadings pointer
			delete newReadings;
		}
		else
		{
			// Filtered data error: use current reading set
			finalData = (ReadingSet *)readingSet;
		}

		// Remove pReturn object
		Py_CLEAR(pReturn);
	}

	// - 4 - Pass (new or old) data set to next filter
	filter->m_func(filter->m_data, finalData);
}

/**
 * Call the shutdown method in the plugin
 */
void plugin_shutdown(PLUGIN_HANDLE *handle)
{
	FogLampFilter* data = (FogLampFilter *)handle;
	delete data;

	// Decrement pModule reference count
	Py_CLEAR(pModule);
	// Decrement pFunc reference count
	Py_CLEAR(pFunc);

	// Cleanup Python 3.5
	Py_Finalize();
}

// End of extern "C"
};

/**
 * Create a Python 3.5 object (list of dicts)
 * to be passed to Python 3.5 loaded filter
 *
 * @param readings	The input readings
 * @return		PyObject pointer (list of dicts)
 *			or NULL in case of errors
 */
static PyObject* createReadingsList(const vector<Reading *>& readings)
{
	// TODO add checks to all PyList_XYZ methods
	PyObject* readingsList = PyList_New(0);

	// Iterate the input readings
	for (vector<Reading *>::const_iterator elem = readings.begin();
                                                      elem != readings.end();
                                                      ++elem)
	{
		// Create an object (dict) with 'asset_code' and 'readings' key
		PyObject* readingObject = PyDict_New();

		// Create object (dict) for reading Datapoints:
		// this will be added as vale for key 'readings'
		PyObject* newDataPoints = PyDict_New();

		// Get all datapoints
		std::vector<Datapoint *>& dataPoints = (*elem)->getReadingData();
		for (auto it = dataPoints.begin(); it != dataPoints.end(); ++it)
		{
			PyObject* value;
			DatapointValue::dataTagType dataType = (*it)->getData().getType();

			if (dataType == DatapointValue::dataTagType::T_INTEGER)
			{
				value = PyLong_FromLong((*it)->getData().toInt());
			}
			else if (dataType == DatapointValue::dataTagType::T_FLOAT)
			{
				value = PyFloat_FromDouble((*it)->getData().toDouble());
			}
			else
			{
				value = PyBytes_FromString((*it)->getData().toString().c_str());
			}

			// Add Datapoint: key and value
			PyDict_SetItemString(newDataPoints,
					     (*it)->getName().c_str(),
					     value);
			Py_CLEAR(value);
		}


		PyObject* assetVal = PyBytes_FromString((*elem)->getAssetName().c_str());
		PyDict_SetItemString(readingObject,
				     "asset_code",
				     assetVal);

		PyDict_SetItemString(readingObject,
				     "reading",
				     newDataPoints);

		// Add new object to the list
		PyList_Append(readingsList, readingObject);

		Py_CLEAR(assetVal);
		Py_CLEAR(newDataPoints);
		Py_CLEAR(readingObject);
	}

	// Return pointer of new allocated list
	return readingsList;
}

/**
 * Get the vector of filtered readings from Python 3.5 script
 *
 * @param filteredData	Python 3.5 Object (list of dicts)
 * @return		Pointer to a new allocated vector<Reading *>
 *			or NULL in case of errors
 * Note:
 * new readings have:
 * - new timestamps
 * - new UUID
 */
static vector<Reading *>* getFilteredReadings(PyObject* filteredData)
{
	// Create result set
	vector<Reading *>* newReadings = new vector<Reading *>();

	// Iterate filtered data in the list
	for (int i = 0; i < PyList_Size(filteredData); i++)
	{
		// Get list item: borrowed reference.
		PyObject* element = PyList_GetItem(filteredData, i);
		if (!element)
		{
			// Failure
			if (PyErr_Occurred())
			{
				logErrorMessage();
			}
			delete newReadings;

			return NULL;
		}

		// Get 'asset_code' value: borrowed reference.
		PyObject* assetCode = PyDict_GetItemString(element,
							   "asset_code");
		// Get 'reading' value: borrowed reference.
		PyObject* reading = PyDict_GetItemString(element,
							 "reading");

		// Keys not found or reading is not a dict
		if (!assetCode ||
		    !reading ||
		    !PyDict_Check(reading))
		{
			// Failure
			if (PyErr_Occurred())
			{
				logErrorMessage();
			}
			delete newReadings;

			return NULL;
		}

		// Fetch all Datapoins in 'reading' dict			
		PyObject *dKey, *dValue;
		Py_ssize_t dPos = 0;
		Reading* newReading = NULL;

		// Fetch all Datapoins in 'reading' dict
		// dKey and dValue are borrowed references
		while (PyDict_Next(reading, &dPos, &dKey, &dValue))
		{
			DatapointValue* dataPoint;
			if (PyLong_Check(dValue) || PyLong_Check(dValue))
			{
				dataPoint = new DatapointValue((long)PyLong_AsUnsignedLongMask(dValue));
			}
			else if (PyFloat_Check(dValue))
			{
				dataPoint = new DatapointValue(PyFloat_AS_DOUBLE(dValue));
			}
			else if (PyBytes_Check(dValue))
			{
				dataPoint = new DatapointValue(string(PyBytes_AsString(dValue)));
			}
			else
			{
				delete newReadings;
				delete dataPoint;

				return NULL;
			}

			// Add / Update the new Reading data			
			if (newReading == NULL)
			{
				newReading = new Reading(PyBytes_AsString(assetCode),
							 new Datapoint(PyBytes_AsString(dKey),
								       *dataPoint));
			}
			else
			{
				newReading->addDatapoint(new Datapoint(PyBytes_AsString(dKey),
								       *dataPoint));
			}

			// Remove temp objects
			delete dataPoint;
		}

		// Add the new reading to result vector
		newReadings->push_back(newReading);
	}

	return newReadings;
}

/**
 * Log current Python 3.5 error message
 *
 */
static void logErrorMessage()
{
	//Get error message
	PyObject *pType, *pValue, *pTraceback;
	PyErr_Fetch(&pType, &pValue, &pTraceback);

	// NOTE from :
	// https://docs.python.org/2/c-api/exceptions.html
	//
	// The value and traceback object may be NULL
	// even when the type object is not.	
	const char* pErrorMessage = pValue ?
				    PyBytes_AsString(pValue) :
				    "no error description.";

	Logger::getLogger()->fatal("Filter '%s', script "
				   "'%s': Error '%s'",
				   FILTER_NAME,
				   pythonScript.c_str(),
				   pErrorMessage ?
				   pErrorMessage :
				   "no description");

	// Reset error
	PyErr_Clear();

	// Remove references
	Py_CLEAR(pType);
	Py_CLEAR(pValue);
	Py_CLEAR(pTraceback);
}
