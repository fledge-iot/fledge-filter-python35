/*
 * Fledge "Python 3.5" filter plugin.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <utils.h>
#include <string>
#include <iostream>
#include <pythonreading.h>
#include <pyruntime.h>

#define PYTHON_SCRIPT_METHOD_PREFIX "_script_"
#define PYTHON_SCRIPT_FILENAME_EXTENSION ".py"
#define SCRIPT_CONFIG_ITEM_NAME "script"
// Filter configuration method
#define DEFAULT_FILTER_CONFIG_METHOD "set_filter_config"

#include "python35.h"

using namespace std;

/**
 * Python filter initialisation
 */
void Python35Filter::init()
{
	// Embedded Python 3.5 program name
	wchar_t *programName = Py_DecodeLocale(m_name.c_str(), NULL);
	Py_SetProgramName(programName);
	PyMem_RawFree(programName);

	// Embedded Python 3.5 initialisation
	PythonRuntime::getPythonRuntime();

	m_init = true;

	PyGILState_STATE state = PyGILState_Ensure(); // acquire GIL

	// Pass Fledge Data dir
	setFiltersPath(getDataDir());

	// Set Python path for embedded Python 3.5
	// Get current sys.path. borrowed reference
	PyObject* sysPath = PySys_GetObject((char *)string("path").c_str());
	// Add Fledge python filters path
	PyObject* pPath = PyUnicode_DecodeFSDefault((char *)getFiltersPath().c_str());
	PyList_Insert(sysPath, 0, pPath);
	// Remove temp object
	Py_CLEAR(pPath);

	// Check first we have a Python script to load
	if (!setScriptName())
	{
		PyGILState_Release(state);
		m_failedScript = true;
		m_execCount = 0;
		return;
	}

	// Configure filter
	lock();
	bool ret = configure();
	unlock();

	if (!ret &&  m_init)
	{
		// Set init failure
		m_init = false;
	}

	PyGILState_Release(state); // release GIL

}

/**
 * Ingest a set of readings into the Python plugin
 *
 * @param readingSet	The set of readings to ingest
 */
void Python35Filter::ingest(READINGSET *readingSet)
{
ReadingSet* finalData = NULL;

	// Protect against reconfiguration
	lock();
	bool enabled = isEnabled();
	unlock();

	if (!enabled)
	{
		// Current filter is not active: just pass the readings set
		m_func(m_data, readingSet);
		return;
	}

	if (m_failedScript)
	{
		if (m_execCount++ > 100)
		{
			m_logger->warn("The %s filter plugin is unable to process data as the supplied Python script has errors.", m_name.c_str());
			m_execCount = 0;
		}
		delete (ReadingSet *)readingSet;
		return;
	}

	AssetTracker *tracker = AssetTracker::getAssetTracker();
	if (!tracker)
	{
		m_logger->warn("Unable to obtian a reference to the asset tracker. Changes will not be tracked");
	}
        // Get all the readings in the readingset
	const vector<Reading *>& readings = ((ReadingSet *)readingSet)->getAllReadings();
	for (vector<Reading *>::const_iterator elem = readings.begin();
						      elem != readings.end();
						      ++elem)
	{
		if (tracker)
		{
			tracker->addAssetTrackingTuple(m_name,
							(*elem)->getAssetName(),
							string("Filter"));
		}
	}
	
	/**
	 * 1 - create a Python object (list of dicts) from input data
	 * 2 - pass Python object to Python filter method
	 * 3 - Transform results from fealter into new ReadingSet
	 * 4 - Remove old data and pass new data set onwards
	 */
	if (! Py_IsInitialized()) {

		m_logger->fatal("The Python environment failed to  initialize, the %s filter is unable to process any data", m_name.c_str());
		delete (ReadingSet *)readingSet;
		return;
	}

	PyGILState_STATE state = PyGILState_Ensure();

	// - 1 - Create Python list of dicts as input to the filter
	PyObject* readingsList = createReadingsList(readings);

	// Check for errors
	if (!readingsList)
	{
		// Errors while creating Python 3.5 filter input object
		m_logger->error("Internal error in the filter %s, unable to create data to be sent to the Python filter function", m_name.c_str());

		PyGILState_Release(state);
		delete (ReadingSet *)readingSet;
		return;
	}

	// - 2 - Call Python method passing an object
	PyObject* pReturn = PyObject_CallFunction(m_pFunc,
						  (char *)string("O").c_str(),
						  readingsList);

	// Free filter input data
	Py_CLEAR(readingsList);


	// - 3 - Handle filter returned data
	if (!pReturn)
	{
		// Errors while getting result object
		logErrorMessage();

		// Failed to get filtered data, pass on empty set of data
		delete (ReadingSet *)readingSet;
		finalData = new ReadingSet();
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

			const vector<Reading *>& readings2 = finalData->getAllReadings();
			if (tracker)
			{
				for (vector<Reading *>::const_iterator elem = readings2.begin();
								      elem != readings2.end();
								      ++elem)
				{
					tracker->addAssetTrackingTuple(m_name,
									(*elem)->getAssetName(),
									string("Filter"));
				}
			}

			// - Remove newReadings pointer
			delete newReadings;
		}
		else
		{
			// Failed to get filtered data, pass on empty set of data
			delete (ReadingSet *)readingSet;
			finalData = new ReadingSet();
		}

		// Remove pReturn object
		Py_CLEAR(pReturn);
	}

	PyGILState_Release(state);

	// - 4 - Pass (new or old) data set to next filter
	m_func(m_data, finalData);
}

/**
 * Shutdown the Python35 filter
 */
void Python35Filter::shutdown()
{
	PyGILState_STATE state = PyGILState_Ensure();

	// Decrement pFunc reference count
	Py_CLEAR(m_pFunc);
		
	// Decrement pModule reference count
	Py_CLEAR(m_pModule);

	m_init = false;

	// Interpreter is still running, just release the GIL
	PyGILState_Release(state);
}

/**
 * Create a Python 3.5 object (list of dicts)
 * to be passed to Python 3.5 loaded filter
 *
 * @param readings	The input readings
 * @return		PyObject pointer (list of dicts)
 *			or NULL in case of errors
 */
PyObject* Python35Filter::createReadingsList(const vector<Reading *>& readings)
{
	// TODO add checks to all PyList_XYZ methods
	PyObject* readingsList = PyList_New(0);

	PyObject *temporary_item = NULL;

	// Iterate the input readings
	for (vector<Reading *>::const_iterator elem = readings.begin();
                                                      elem != readings.end();
                                                      ++elem)
	{
		// Create PythonReading object from readings data	
		PythonReading *pyReading = (PythonReading *)(*elem);

		// Add new PythonReading object to the output list
		// Passing first parameter as true, sets keys to "reading" and "asset_code"
		// Passing second parameter as strue, sets Bytes string for backwards compatibility
		// for DICT keys and string values

		temporary_item = pyReading->toPython(true, m_encode_names);

		PyList_Append(readingsList, temporary_item);

		Py_DECREF(temporary_item);
		temporary_item =  NULL;
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
vector<Reading *>* Python35Filter::getFilteredReadings(PyObject* filteredData)
{
	// Create result set
	vector<Reading *>* newReadings = new vector<Reading *>();

	// Allow None to mean that no readings are returned
	if (filteredData == Py_None)
	{
		return newReadings;
	}

	if (PyList_Check(filteredData))
	{
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
					this->logErrorMessage();
				}
				delete newReadings;

				return NULL;
			}

			if (PyDict_Check(element))
			{

				// Create Reading object from Python object in the list
				try {
					Reading *reading = new PythonReading(element);

					if (reading)
					{
						// Add the new reading to result vector
						newReadings->push_back(reading);
					}
				} catch (exception &e) {
					m_logger->error("Badly formed reading in list returned by the Python script: %s", e.what());
					delete newReadings;
					return NULL;
				}
			}
			else
			{
				m_logger->error("Each element returned by the script must be a Python DICT");
				delete newReadings;
				return NULL;
			}
		}

		return newReadings;
	}
	else
	{
		m_logger->error("The return type of the python35 filter function should be a list of readings.");
		delete newReadings;
		return NULL;
	}

}

/**
 * Log an error from the Python interpreter
 */
void Python35Filter::logErrorMessage()
{
PyObject *ptype, *pvalue, *ptraceback;

	if (PyErr_Occurred())
	{
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);
		PyErr_NormalizeException(&ptype,&pvalue,&ptraceback);

		char *msg, *file, *text;
		int line, offset;

		int res = PyArg_ParseTuple(pvalue,"s(siis)",&msg,&file,&line,&offset,&text);

		PyObject *line_no = PyObject_GetAttrString(pvalue,"lineno");
		PyObject *line_no_str = PyObject_Str(line_no);
		PyObject *line_no_unicode = PyUnicode_AsEncodedString(line_no_str,"utf-8", "Error");
		char *actual_line_no = PyBytes_AsString(line_no_unicode);  // Line number

		PyObject *ptext = PyObject_GetAttrString(pvalue,"text");
		PyObject *ptext_str = PyObject_Str(ptext);
		PyObject *ptext_no_unicode = PyUnicode_AsEncodedString(ptext_str,"utf-8", "Error");
		char *error_line = PyBytes_AsString(ptext_no_unicode);  // Line in error

		// Remove the trailing newline from the string
		char *newline = rindex(error_line,  '\n');
		if (newline)
		{
			*newline = '\0';
		}

		// Not managed to find a way to get the actual error message from Python
		// so use the string representation of the Error class and tidy it up, e.g.
		// SyntaxError('invalid syntax', ('/tmp/scripts/test_addition_script_script.py', 9, 1, '}\n'))
		PyObject *pstr = PyObject_Repr(pvalue);
		PyObject *perr = PyUnicode_AsEncodedString(pstr, "utf-8", "Error");
		char *err_msg = PyBytes_AsString(perr);
		char *end = index(err_msg, ',');
		if (end)
		{
			*end = '\0';
		}
		end = index(err_msg, '(');
		if (end)
		{
			*end = ' ';
		}


		if (error_line == NULL || actual_line_no == NULL || strcmp(error_line, "<NULL>") == 0
			       	|| strcmp(actual_line_no, "<NULL>") == 0)
		{
			m_logger->error("Python error: %s in supplied script", err_msg);
		}
		else
		{
			m_logger->error("Python error: %s in %s at line %s of supplied script", err_msg, error_line, actual_line_no);
		}

		PyErr_Clear();
		Py_CLEAR(line_no);
		Py_CLEAR(line_no_str);
		Py_CLEAR(line_no_unicode);
		Py_CLEAR(ptext);
		Py_CLEAR(ptext_str);
		Py_CLEAR(ptext_no_unicode);
		Py_CLEAR(pstr);
		Py_CLEAR(perr);
	}
}

/**
 * Reconfigure Python35 filter with new configuration
 *
 * @param    newConfig		The new configuration
 *				from "plugin_reconfigure"
 * @return			True on success, false on errors.
 */
bool Python35Filter::reconfigure(const string& newConfig)
{
	m_logger->debug("%s filter 'plugin_reconfigure' called = %s",
				   this->getName().c_str(),
				   newConfig.c_str());

	ConfigCategory category("new", newConfig);
	string newScript;

	// Configuration change is protected by a lock
	lock_guard<mutex> guard(m_configMutex);

	PyGILState_STATE state = PyGILState_Ensure(); // acquire GIL

	// Get Python script file from "file" attibute of "scipt" item
	if (category.itemExists(SCRIPT_CONFIG_ITEM_NAME))
	{
		try
		{
			// Get Python script file from "file" attibute of "scipt" item
			newScript = category.getItemAttribute(SCRIPT_CONFIG_ITEM_NAME,
								   ConfigCategory::FILE_ATTR);

			// Just take file name and remove path
			std::size_t found = newScript.find_last_of("/");
			if (found != std::string::npos)
			{
				newScript = newScript.substr(found + 1);

				// Remove .py from pythonScript
				found = newScript.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
				if (found != std::string::npos)
				{
					newScript.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
				}
			}
		}
		catch (ConfigItemAttributeNotFound* e)
		{
			delete e;
		}
		catch (exception* e)
		{
			delete e;
		}
	}

	if (newScript.empty())
	{
		m_logger->warn("Filter '%s', "
					  "called without a Python 3.5 script. "
					  "Check 'script' item in '%s' configuration. "
					  "Filter has been disabled.",
					  this->getName().c_str(),
					  this->getName().c_str());
		// Force disable
		PyGILState_Release(state);
		this->disableFilter();
		return false;
	}

	// Reload module or Import module ?
	if (newScript.compare(m_pythonScript) == 0 && m_pModule)
	{
		m_failedScript = false;
		m_execCount = 0;
		// Reimport module
		PyObject* newModule = PyImport_ReloadModule(m_pModule);
		if (newModule)
		{
			// Cleanup Loaded module
			Py_CLEAR(m_pModule);
			m_pModule = NULL;
			Py_CLEAR(m_pFunc);
			m_pFunc = NULL;

			// Set new name
			m_pythonScript = newScript;

			// Set reloaded module
			m_pModule = newModule;
		}
		else
		{
			// Errors while reloading the Python module
			m_logger->error("%s filter error while reloading "
						   " Python script '%s' in 'plugin_reconfigure'",
						   this->getName().c_str(),
						   m_pythonScript.c_str());
			logErrorMessage();

			PyGILState_Release(state);
			m_failedScript = true;

			return false;
		}
	}
	else
	{
		m_failedScript = false;
		// Import the new module

		// Cleanup Loaded module
		Py_CLEAR(m_pModule);
		m_pModule = NULL;
		Py_CLEAR(m_pFunc);
		m_pFunc = NULL;

		// Set new name
		m_pythonScript = newScript;

		// Import the new module
		PyObject* newModule = PyImport_ImportModule(m_pythonScript.c_str());

		// Set reloaded module
		m_pModule = newModule;
	}

	// Set the enable flag
	if (category.itemExists("enable"))
	{
		m_enabled = category.getValue("enable").compare("true") == 0 ||
				category.getValue("enable").compare("True") == 0;
	}

	// Set encode/decode attribute names for compatibility
	if (category.itemExists("encode_attribute_names"))
	{
		m_encode_names = category.getValue("encode_attribute_names").compare("true") == 0 ||
				category.getValue("encode_attribute_names").compare("True") == 0;
	}

	bool ret = this->configure();

	// Whole configuration as it is
	string filterConfiguration;

	// Get 'config' filter category configuration
	if (category.itemExists("config"))
	{
		filterConfiguration = category.getValue("config");
	}
	else
	{
		// Set empty object
		filterConfiguration = "{}";
	}
	/**
	 * We now pass the filter JSON configuration to the loaded module
	 */
	if (m_pModule)
	{
		PyObject* pConfigFunc = PyObject_GetAttrString(m_pModule,
								   (char *)string(DEFAULT_FILTER_CONFIG_METHOD).c_str());
		// Check whether "set_filter_config" method exists
		if (PyCallable_Check(pConfigFunc))
		{
			// Set configuration object 
			PyObject* pConfig = PyDict_New();
			// Add JSON configuration, as string, to "config" key
			PyObject* pConfigObject = PyUnicode_DecodeFSDefault(filterConfiguration.c_str());
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
			//PyObject* pSetConfig = PyObject_CallMethod(pModule,
			PyObject* pSetConfig = PyObject_CallFunctionObjArgs(pConfigFunc,
										// arg 1
										pConfig,
										// end of args
										NULL);

			// Check result
			if (!pSetConfig ||
				!PyBool_Check(pSetConfig) ||
				!PyLong_AsLong(pSetConfig))
			{
				this->logErrorMessage();

				Py_CLEAR(m_pModule);
				m_pModule = NULL;
				Py_CLEAR(m_pFunc);
				m_pFunc = NULL;
				// Remove temp objects
				Py_CLEAR(pConfig);
				Py_CLEAR(pSetConfig);

				// Remove function object
				Py_CLEAR(pConfigFunc);

				return false;
			}
			// Remove call object
			Py_CLEAR(pSetConfig);
			// Remove temp objects
			Py_CLEAR(pConfig);
		}
		else
		{
			// Reset error if config function is not present
			PyErr_Clear();
		}

		// Remove function object
		Py_CLEAR(pConfigFunc);
	}

	PyGILState_Release(state);

	return ret;
}


/**
 * Configure Python35 filter:
 *
 * import the Python script file and call
 * script configuration method with current filter configuration
 *
 * @return	True on success, false on errors.
 */
bool Python35Filter::configure()
{
	m_failedScript = false;
	// Set encode/decode attribute names for compatibility
	if (this->getConfig().itemExists("encode_attribute_names"))
	{
		m_encode_names = this->getConfig().getValue("encode_attribute_names").compare("true") == 0 ||
					this->getConfig().getValue("encode_attribute_names").compare("True") == 0;
	}

	// Import script as module
	// NOTE:
	// Script file name is:
	// lowercase(categoryName) + _script_ + methodName + ".py"
	
	string filterMethod;
	std::size_t found;

	m_logger->debug("%s:%d: m_pythonScript=%s", __FUNCTION__, __LINE__, m_pythonScript.c_str());

	// 1) Get methodName
	found = m_pythonScript.rfind(PYTHON_SCRIPT_METHOD_PREFIX);
	if (found != std::string::npos)
	{
		filterMethod = m_pythonScript.substr(found + strlen(PYTHON_SCRIPT_METHOD_PREFIX));
	}
	// Remove .py from filterMethod
	found = filterMethod.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	if (found != std::string::npos)
	{
		filterMethod.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
	}
	// Remove .py from pythonScript
	found = m_pythonScript.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	if (found != std::string::npos)
	{
		m_pythonScript.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
	}
	
	m_logger->debug("%s filter: script='%s', method='%s'",
				   this->getName().c_str(),
				   m_pythonScript.c_str(),
				   filterMethod.c_str());

	// 2) Import Python script
	// check first method name is empty:
	// disable filter, cleanup and return true
	// This allows reconfiguration
	if (filterMethod.empty())
	{
		// Force disable
		this->disableFilter();

		m_pModule = NULL;
		m_pFunc = NULL;

		return true;
	}

	// 2) Import Python script if module object is not set
	if (!m_pModule)
	{
		m_pModule = PyImport_ImportModule(m_pythonScript.c_str());
	}

	// Check whether the Python module has been imported
	if (!m_pModule)
	{
		// Failure
		if (PyErr_Occurred())
		{
			this->logErrorMessage();
		}
		// This will abort the filter pipeline set up
		m_failedScript = true;
		return false;
	}

	// Fetch filter method in loaded object
	m_pFunc = PyObject_GetAttrString(m_pModule, filterMethod.c_str());

	if (!PyCallable_Check(m_pFunc))
	{
		// Failure
		if (PyErr_Occurred())
		{
			this->logErrorMessage();
		}

		Py_CLEAR(m_pModule);
		m_pModule = NULL;
		Py_CLEAR(m_pFunc);
		m_pFunc = NULL;

		m_failedScript = true;

		// This will abort the filter pipeline set up
		return false;
	}

	// Whole configuration as it is
	string filterConfiguration;

	// Get 'config' filter category configuration
	if (this->getConfig().itemExists("config"))
	{
		filterConfiguration = this->getConfig().getValue("config");
	}
	else
	{
		// Set empty object
		filterConfiguration = "{}";
	}

	/**
	 * We now pass the filter JSON configuration to the loaded module
	 */
	PyObject* pConfigFunc = PyObject_GetAttrString(m_pModule,
							   (char *)string(DEFAULT_FILTER_CONFIG_METHOD).c_str());
	// Check whether "set_filter_config" method exists
	if (PyCallable_Check(pConfigFunc))
	{
		// Set configuration object 
		PyObject* pConfig = PyDict_New();
		// Add JSON configuration, as string, to "config" key
		PyObject* pConfigObject = PyUnicode_DecodeFSDefault(filterConfiguration.c_str());
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
		//PyObject* pSetConfig = PyObject_CallMethod(pModule,
		PyObject* pSetConfig = PyObject_CallFunctionObjArgs(pConfigFunc,
									// arg 1
									pConfig,
									// end of args
									NULL);

		// Check result
		if (!pSetConfig ||
			!PyBool_Check(pSetConfig) ||
			!PyLong_AsLong(pSetConfig))
		{
			this->logErrorMessage();

			Py_CLEAR(m_pModule);
			m_pModule = NULL;
			Py_CLEAR(m_pFunc);
			m_pFunc = NULL;
			// Remove temp objects
			Py_CLEAR(pConfig);
			Py_CLEAR(pSetConfig);

			// Remove function object
			Py_CLEAR(pConfigFunc);

			return false;
		}
		// Remove call object
		Py_CLEAR(pSetConfig);
		// Remove temp objects
		Py_CLEAR(pConfig);
	}
	else
	{
		// Reset error if config function is not present
		PyErr_Clear();
	}

	// Remove function object
	Py_CLEAR(pConfigFunc);

	return true;
}

/**
 * Set the Python script name to load.
 *
 * If the attribute "file" of "script" items exists
 * in input configuration the m_pythonScript member is updated.
 *
 * This method must be called before Python35Filter::configure()
 *
 * @return	True if script file exists, false otherwise
 */
bool Python35Filter::setScriptName()
{
	// Check whether we have a Python 3.5 script file to import
	if (this->getConfig().itemExists(SCRIPT_CONFIG_ITEM_NAME))
	{
		try
		{
			// Get Python script file from "file" attibute of "script" item
			m_pythonScript =
				this->getConfig().getItemAttribute(SCRIPT_CONFIG_ITEM_NAME,
								   ConfigCategory::FILE_ATTR);
			m_logger->debug("Got script %s", m_pythonScript.c_str());
			// Just take file name and remove path
			std::size_t found = m_pythonScript.find_last_of("/");
			m_pythonScript = m_pythonScript.substr(found + 1);
		}
		catch (ConfigItemAttributeNotFound* e)
		{
			delete e;
		}
		catch (exception* e)
		{
			delete e;
		}
	}
	else
	{
		m_logger->error("There is no item named '%s' in the plugin configuration",
				SCRIPT_CONFIG_ITEM_NAME);
	}

	if (m_pythonScript.empty())
	{
		// Do nothing
		m_logger->warn("Filter '%s', "
					  "called without a Python 3.5 script. "
					  "Check 'script' item in '%s' configuration. "
					  "Filter has been disabled.",
					  this->getName().c_str(),
					  this->getConfig().getName().c_str());
	}

	return !m_pythonScript.empty();
}

/**
 * Fix the quoting if the datapoint contians unescaped quotes
 *
 * @param str	Strign to fix the quoting of
 */
void Python35Filter::fixQuoting(string& str)
{
string newString;
bool escape = false;

	for (int i = 0; i < str.length(); i++)
	{
		if (str[i] == '\"' && escape == false)
		{
			newString += '\\';
			newString += '\\';
			newString += '\\';
		}
		else if (str[i] == '\\')
		{
			escape = !escape;
		}
		newString += str[i];
	}
	str = newString;
}
