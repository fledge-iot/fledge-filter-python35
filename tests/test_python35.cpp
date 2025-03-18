#include <gtest/gtest.h>
#include <plugin_api.h>
#include <config_category.h>
#include <filter_plugin.h>
#include <filter.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <rapidjson/document.h>
#include <reading.h>
#include <reading_set.h>
#include <python35.h>

using namespace std;
using namespace rapidjson;

namespace {
const char *addition_script = R"(
def script(readings):
    for elem in list(readings):
        reading = elem['reading']
        sum = reading[b'a'] + reading[b'b']
        reading[b'sum'] = sum
    return readings
)";

const char *none_script = R"(
def script(readings):
    return None
)";

const char *bad_reading_script = R"(
def script(readings):
    for elem in list(readings):
        del elem[b'asset_code']
    return readings
)";

const char *wrong_type_script = R"(
def script(readings):
    return ""
)";

const char *indent_error_script = R"(
def script(readings):
return readings
)";

const char *reconfig_script = R"(
import json  

filter_config = None   

def set_filter_config(configuration):
    global filter_config
    filter_config = json.loads(configuration['config'])
    return True

def script(readings):   
    global filter_config
    for item in readings:
        item['asset_code']=item['asset_code'] + "modified" + filter_config['suffix']
    return readings
)";

extern "C" {
	PLUGIN_INFORMATION *plugin_info();
	void plugin_ingest(void *handle,
                   READINGSET *readingSet);
	PLUGIN_HANDLE plugin_init(ConfigCategory* config,
			  OUTPUT_HANDLE *outHandle,
			  OUTPUT_STREAM output);
	void plugin_reconfigure(void *handle, const string& newConfig);
	void plugin_shutdown(PLUGIN_HANDLE handle);
	int called = 0;

	void Handler(void *handle, READINGSET *readings)
	{
		called++;
		*(READINGSET **)handle = readings;
	}
};

TEST(PYTHON35, Addition)
{
	setenv("FLEDGE_DATA", "/tmp", 1);
	mkdir("/tmp/scripts", 0777);

	PLUGIN_INFORMATION *info = plugin_info();
	ConfigCategory *config = new ConfigCategory("script", info->config);
	ASSERT_NE(config, (ConfigCategory *)NULL);
	config->setItemsValueFromDefault();
	const char *script = "/tmp/scripts/test_addition_script_script.py";
	FILE *fp = fopen(script, "w");
	ASSERT_NE(fp, (FILE *)0);
	fprintf(fp, "%s", addition_script);
	fclose(fp);
	ASSERT_EQ(config->itemExists("script"), true);
	config->setValue("script", addition_script);
	config->setItemAttribute("script", ConfigCategory::FILE_ATTR, script);
	config->setValue("enable", "true");
	ReadingSet *outReadings;
	void *handle = plugin_init(config, &outReadings, Handler);
	ASSERT_NE(handle, (void *)NULL);
	vector<Reading *> *readings = new vector<Reading *>;

	vector<Datapoint *> datapoints;
	long a = 1000;
	DatapointValue dpv(a);
	datapoints.push_back(new Datapoint("a", dpv));
	long b = 50;
	DatapointValue dpv1(b);
	datapoints.push_back(new Datapoint("b", dpv1));
	readings->push_back(new Reading("test", datapoints));


	ReadingSet *readingSet = new ReadingSet(readings);
	delete readings;
	plugin_ingest(handle, (READINGSET *)readingSet);


	vector<Reading *>results = outReadings->getAllReadings();
	ASSERT_EQ(results.size(), 1);
	Reading *out = results[0];
	ASSERT_STREQ(out->getAssetName().c_str(), "test");
	ASSERT_EQ(out->getDatapointCount(), 3);
	vector<Datapoint *> points = out->getReadingData();
	ASSERT_EQ(points.size(), 3);
	for (int i = 0; i < 3; i++)
	{
		Datapoint *outdp = points[i];
		if (outdp->getName().compare("a") == 0)
		{
			ASSERT_EQ(outdp->getData().getType(), DatapointValue::T_INTEGER);
			ASSERT_EQ(outdp->getData().toInt(), 1000);
		}
		else if (outdp->getName().compare("b") == 0)
		{
			ASSERT_EQ(outdp->getData().getType(), DatapointValue::T_INTEGER);
			ASSERT_EQ(outdp->getData().toInt(), 50);
		}
		else if (outdp->getName().compare("sum") == 0)
		{
			ASSERT_EQ(outdp->getData().getType(), DatapointValue::T_INTEGER);
			ASSERT_EQ(outdp->getData().toInt(), 1050);
		}
		else
		{
			ASSERT_STREQ(outdp->getName().c_str(), "result");
		}
	}

	// Cleanup
	delete config;
	delete outReadings;
	plugin_shutdown(handle);
}

TEST(PYTHON35, None)
{
	setenv("FLEDGE_DATA", "/tmp", 1);
	mkdir("/tmp/scripts", 0777);

	PLUGIN_INFORMATION *info = plugin_info();
	ConfigCategory *config = new ConfigCategory("script", info->config);
	ASSERT_NE(config, (ConfigCategory *)NULL);
	config->setItemsValueFromDefault();
	const char *script = "/tmp/scripts/test_none_script_script.py";
	FILE *fp = fopen(script, "w");
	ASSERT_NE(fp, (FILE *)0);
	fprintf(fp, "%s", none_script);
	fclose(fp);
	ASSERT_EQ(config->itemExists("script"), true);
	config->setValue("script", none_script);
	config->setItemAttribute("script", ConfigCategory::FILE_ATTR, script);
	config->setValue("enable", "true");
	ReadingSet *outReadings;
	void *handle = plugin_init(config, &outReadings, Handler);
	ASSERT_NE(handle, (void *)NULL);
	vector<Reading *> *readings = new vector<Reading *>;

	vector<Datapoint *> datapoints;
	long a = 1000;
	DatapointValue dpv(a);
	datapoints.push_back(new Datapoint("a", dpv));
	long b = 50;
	DatapointValue dpv1(b);
	datapoints.push_back(new Datapoint("b", dpv1));
	readings->push_back(new Reading("test", datapoints));


	ReadingSet *readingSet = new ReadingSet(readings);
	delete readings;
	plugin_ingest(handle, (READINGSET *)readingSet);


	vector<Reading *>results = outReadings->getAllReadings();
	ASSERT_EQ(results.size(), 0);

	// Cleanup
	delete config;
	delete outReadings;
	plugin_shutdown(handle);
}

TEST(PYTHON35, BadReading)
{
	setenv("FLEDGE_DATA", "/tmp", 1);
	mkdir("/tmp/scripts", 0777);

	PLUGIN_INFORMATION *info = plugin_info();
	ConfigCategory *config = new ConfigCategory("script", info->config);
	ASSERT_NE(config, (ConfigCategory *)NULL);
	config->setItemsValueFromDefault();
	const char *script = "/tmp/scripts/test_badreading_script_script.py";
	FILE *fp = fopen(script, "w");
	ASSERT_NE(fp, (FILE *)0);
	fprintf(fp, "%s", bad_reading_script);
	fclose(fp);
	ASSERT_EQ(config->itemExists("script"), true);
	config->setValue("script", bad_reading_script);
	config->setItemAttribute("script", ConfigCategory::FILE_ATTR, script);
	config->setValue("enable", "true");
	ReadingSet *outReadings;
	void *handle = plugin_init(config, &outReadings, Handler);
	ASSERT_NE(handle, (void *)NULL);
	vector<Reading *> *readings = new vector<Reading *>;

	vector<Datapoint *> datapoints;
	long a = 1000;
	DatapointValue dpv(a);
	datapoints.push_back(new Datapoint("a", dpv));
	long b = 50;
	DatapointValue dpv1(b);
	datapoints.push_back(new Datapoint("b", dpv1));
	readings->push_back(new Reading("test", datapoints));


	ReadingSet *readingSet = new ReadingSet(readings);
	delete readings;
	plugin_ingest(handle, (READINGSET *)readingSet);


	vector<Reading *>results = outReadings->getAllReadings();
	ASSERT_EQ(results.size(), 0);

	// Cleanup
	delete config;
	delete outReadings;
	plugin_shutdown(handle);
}

TEST(PYTHON35, WrongType)
{
	setenv("FLEDGE_DATA", "/tmp", 1);
	mkdir("/tmp/scripts", 0777);

	PLUGIN_INFORMATION *info = plugin_info();
	ConfigCategory *config = new ConfigCategory("script", info->config);
	ASSERT_NE(config, (ConfigCategory *)NULL);
	config->setItemsValueFromDefault();
	const char *script = "/tmp/scripts/test_wrongtype_script_script.py";
	FILE *fp = fopen(script, "w");
	ASSERT_NE(fp, (FILE *)0);
	fprintf(fp, "%s", wrong_type_script);
	fclose(fp);
	ASSERT_EQ(config->itemExists("script"), true);
	config->setValue("script", wrong_type_script);
	config->setItemAttribute("script", ConfigCategory::FILE_ATTR, script);
	config->setValue("enable", "true");
	ReadingSet *outReadings;
	void *handle = plugin_init(config, &outReadings, Handler);
	ASSERT_NE(handle, (void *)NULL);
	vector<Reading *> *readings = new vector<Reading *>;

	vector<Datapoint *> datapoints;
	long a = 1000;
	DatapointValue dpv(a);
	datapoints.push_back(new Datapoint("a", dpv));
	long b = 50;
	DatapointValue dpv1(b);
	datapoints.push_back(new Datapoint("b", dpv1));
	readings->push_back(new Reading("test", datapoints));


	ReadingSet *readingSet = new ReadingSet(readings);
	delete readings;
	plugin_ingest(handle, (READINGSET *)readingSet);


	vector<Reading *>results = outReadings->getAllReadings();
	ASSERT_EQ(results.size(), 0);

	// Cleanup
	delete config;
	delete outReadings;
	plugin_shutdown(handle);
}

TEST(PYTHON35, IndentError)
{
	setenv("FLEDGE_DATA", "/tmp", 1);
	mkdir("/tmp/scripts", 0777);

	PLUGIN_INFORMATION *info = plugin_info();
	ConfigCategory *config = new ConfigCategory("script", info->config);
	ASSERT_NE(config, (ConfigCategory *)NULL);
	config->setItemsValueFromDefault();
	const char *script = "/tmp/scripts/test_indenterror_script_script.py";
	FILE *fp = fopen(script, "w");
	ASSERT_NE(fp, (FILE *)0);
	fprintf(fp, "%s", indent_error_script);
	fclose(fp);
	ASSERT_EQ(config->itemExists("script"), true);
	config->setValue("script", indent_error_script);
	config->setItemAttribute("script", ConfigCategory::FILE_ATTR, script);
	config->setValue("enable", "true");
	ReadingSet *outReadings;
	void *handle = plugin_init(config, &outReadings, Handler);
	Python35Filter *hndl = (Python35Filter *) handle;
	// handle is valid but it has not been configured/init properly/completely because of indent error in python script
	ASSERT_FALSE(hndl && hndl->initSuccess());

	// Cleanup
	delete config;
	plugin_shutdown(handle);
}

#if 0
Currently this can not be run because of an issue with gettign a string
variabnt of the configuration category. This is not a plugin issue and needs
to be resolved elsewhere

TEST(PYTHON35, ReconfigScript)
{
	setenv("FLEDGE_DATA", "/tmp", 1);
	mkdir("/tmp/scripts", 0777);

	PLUGIN_INFORMATION *info = plugin_info();
	ConfigCategory *config = new ConfigCategory("script", info->config);
	ASSERT_NE(config, (ConfigCategory *)NULL);
	config->setItemsValueFromDefault();
	const char *script = "/tmp/scripts/test_reconfigscript_script_script.py";
	FILE *fp = fopen(script, "w");
	ASSERT_NE(fp, (FILE *)0);
	fprintf(fp, "%s", reconfig_script);
	fclose(fp);
	ASSERT_EQ(config->itemExists("script"), true);
	config->setValue("script", reconfig_script);
	config->setItemAttribute("script", ConfigCategory::FILE_ATTR, script);
	ASSERT_EQ(config->itemExists("encode_attribute_names"), true);
	config->setValue("encode_attribute_names", "false");
	config->setValue("enable", "true");
	ASSERT_EQ(config->itemExists("config"), true);
	config->setValue("config", "{ \"suffix\" : \"10\" }");
	ReadingSet *outReadings;
	void *handle = plugin_init(config, &outReadings, Handler);
	ASSERT_NE(handle, (void *)NULL);
	vector<Reading *> *readings = new vector<Reading *>;

	vector<Datapoint *> datapoints;
	long a = 1000;
	DatapointValue dpv(a);
	datapoints.push_back(new Datapoint("a", dpv));
	long b = 50;
	DatapointValue dpv1(b);
	datapoints.push_back(new Datapoint("b", dpv1));
	readings->push_back(new Reading("test", datapoints));


	ReadingSet *readingSet = new ReadingSet(readings);
	delete readings;
	plugin_ingest(handle, (READINGSET *)readingSet);


	vector<Reading *>results = outReadings->getAllReadings();
	ASSERT_EQ(results.size(), 1);
	Reading *out = results[0];
	ASSERT_STREQ(out->getAssetName().c_str(), "testmodified10");
	ASSERT_EQ(out->getDatapointCount(), 2);
	vector<Datapoint *> points = out->getReadingData();
	ASSERT_EQ(points.size(), 2);
	for (int i = 0; i < 2; i++)
	{
		Datapoint *outdp = points[i];
		if (outdp->getName().compare("a") == 0)
		{
			ASSERT_EQ(outdp->getData().getType(), DatapointValue::T_INTEGER);
			ASSERT_EQ(outdp->getData().toInt(), 1000);
		}
		else if (outdp->getName().compare("b") == 0)
		{
			ASSERT_EQ(outdp->getData().getType(), DatapointValue::T_INTEGER);
			ASSERT_EQ(outdp->getData().toInt(), 50);
		}
	}

	ConfigCategory reconfig("script", info->config);
	ASSERT_NE(config, (ConfigCategory *)NULL);
	reconfig.setItemsValueFromDefault();
	fp = fopen(script, "w");
	ASSERT_NE(fp, (FILE *)0);
	fprintf(fp, "%s", reconfig_script);
	fclose(fp);
	ASSERT_EQ(reconfig.itemExists("script"), true);
	reconfig.setValue("script", reconfig_script);
	reconfig.setItemAttribute("script", ConfigCategory::FILE_ATTR, script);
	ASSERT_EQ(config->itemExists("encode_attribute_names"), true);
	reconfig.setValue("encode_attribute_names", "false");
	reconfig.setValue("enable", "true");
	reconfig.setValue("config", "{ \"suffix\" : \"5\" }");
	string newConfig = reconfig.itemsToJSON(true);
	plugin_reconfigure(handle, newConfig);

	vector<Reading *> *readings2 = new vector<Reading *>;

	vector<Datapoint *> datapoints2;
	a = 1000;
	DatapointValue dpv2(a);
	datapoints.push_back(new Datapoint("a", dpv2));
	b = 50;
	DatapointValue dpv3(b);
	datapoints.push_back(new Datapoint("b", dpv3));
	readings2->push_back(new Reading("test", datapoints2));


	ReadingSet *readingSet2 = new ReadingSet(readings2);
	delete readings2;
	plugin_ingest(handle, (READINGSET *)readingSet2);

	results = outReadings->getAllReadings();
	ASSERT_EQ(results.size(), 1);
	out = results[0];
	ASSERT_STREQ(out->getAssetName().c_str(), "testmodified5");
	ASSERT_EQ(out->getDatapointCount(), 2);
	points = out->getReadingData();
	ASSERT_EQ(points.size(), 2);
	for (int i = 0; i < 2; i++)
	{
		Datapoint *outdp = points[i];
		if (outdp->getName().compare("a") == 0)
		{
			ASSERT_EQ(outdp->getData().getType(), DatapointValue::T_INTEGER);
			ASSERT_EQ(outdp->getData().toInt(), 1000);
		}
		else if (outdp->getName().compare("b") == 0)
		{
			ASSERT_EQ(outdp->getData().getType(), DatapointValue::T_INTEGER);
			ASSERT_EQ(outdp->getData().toInt(), 50);
		}
	}

	// Cleanup
	delete config;
	delete outReadings;
	plugin_shutdown(handle);
}
#endif
};
