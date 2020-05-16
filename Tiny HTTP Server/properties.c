/*
 * properties.c
 *
 * Functions that implement simple property lists.
 *
 *  @since 2019-04-10
 *  @author: Philip Gust
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "http_server.h"
#include "properties.h"

/** Definition of an entry in a property list */
typedef struct Property {
	char *name; /** name of property */
	char *val;  /** value of property */
} Property;

/** Definition of a property list */
typedef struct Properties {
	Property *props;  			/** array of properties */
	size_t nprops;				/** number of properties */
	size_t maxprops;			/** max number of properties */
} Properties;

/**
 * Create a new properties.
 * @return a new properties
 */
Properties *newProperties() {
	Properties *props = malloc(sizeof(Properties));
	props->maxprops = 4;
	props->nprops  = 0;
	props->props = malloc(props->maxprops*sizeof(Property));
	return props;
}

/**
 * Delete a properties
 * @param a properties
 */
void deleteProperties(Properties *props) {
	for (int i = 0; i < props->nprops; i++) {
		free(props->props[i].name);
		free(props->props[i].val);
	}
	props->nprops = 0;
	props->maxprops = 0;
	free(props->props);
	free(props);
}

/**
 * Put a property to the properties.
 * @param a properties
 * @param name a property name
 * @param val a property value
 * @return true if property added
 */
bool putProperty(Properties *props, const char *name, const char *val) {
	if (props->nprops >= props->maxprops) { // resize if out of space
		props->maxprops *= 2;
		props->props = realloc(props->props, props->maxprops*sizeof(Property));
		if (props->props == NULL) {
			perror("putProperty");
			exit(1);
		}
	}
	props->props[props->nprops].name = strndup(name, MAX_PROP_NAME-1);
	props->props[props->nprops].val = strndup(val, MAX_PROP_VAL-1);

	props->nprops++;
	return true;
}

/**
 * Get name and value for the specified property index.
 * @param props a properties
 * @param propIndex the property index
 * @param name storage for the name
 * @param val storage for the value
 * @return true if property at specified index is available
 */
bool getProperty(Properties *props, size_t propIndex, char *name, char *val) {
	if (propIndex >= props->nprops) {
		return false;
	}
	strcpy(name, props->props[propIndex].name);
	strcpy(val, props->props[propIndex].val);
	return true;
}

/**
 * Find a property by name, starting with specified property index.
 * @param props the properties
 * @param propIndex the starting property index
 * @param name prop name
 * @param val storage for the value
 * @return the index of the value found or SIZE_MAX if not found
 */
size_t findProperty(Properties *props, size_t propIndex, const char *name, char *val) {
	while (propIndex < props->nprops) {
		if (strcasecmp(name, props->props[propIndex].name) == 0) {
			strcpy(val, props->props[propIndex].val);
			return propIndex;
		}
		propIndex++;
	}
	return SIZE_MAX;
}

/**
 * Return number of properties.
 * @param props the properties
 * @return number of properties;
 */
size_t nProperties(const Properties *props) {
	return props->nprops;
}

/**
 * Store properties to properties file.
 *
 * @paam propFile the properties file
 * @param props the properties
 * @return 0 if successful, -1 if cannot create file
 */
bool storeProperties(const char *propFile, Properties *props) {
	FILE *propStream = fopen(propFile, "w");
	if (propStream == NULL) {
		return -1;
	}

	// get next line
	int nprops = props->nprops;
	fprintf(propStream, "# Properties size=%d\n", nprops);
	for (int i = 0; i < nprops; i++) {
		fprintf(propStream, "%s=%s\n", props->props[i].name, props->props[i].val);
	}
	fclose(propStream);
	return 0;
}

/**
 * Load properties from properties file.
 *
 * @paam propFile the properties file
 * @param props the properties
 * @return number of properties read
 */
int loadProperties(const char *propFile, Properties *props) {
	FILE *propStream = fopen(propFile, "r");
	if (propStream == NULL) {
		return 0;
	}
	char buf[MAXBUF];
	int nprops = 0;

	// get next line
	while (fgets(buf, MAXBUF, propStream) != NULL) {
		if (buf[0] == '#') { // ignore comment
			continue;
		}
		char *p = strchr(buf, '=');
		if (p != NULL) {
			*p++ = '\0';
			// make property entry
			putProperty(props, buf, p);
			nprops++;
		}
	}
	fclose(propStream);
	return nprops;
}
