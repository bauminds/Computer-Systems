/*
 * mime_util.h
 *
 * Functions for processing MIME types.
 *
 *  @since 2019-04-10
 *  @author: Philip Gust
 */
#include <stdbool.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "map.h"


#ifndef MIME_UTIL_H_
#define MIME_UTIL_H_

/** declare the map */
map_base_t mime_map;

void buildMap(FILE* mime, map_base_t *mime_map);
/**
 * Return a MIME type for a given filename.
 *
 * @param filename the name of the file
 * @param mimeType output buffer for mime type
 * @return pointer to mime type string
 */
char *getMimeType(const char *filename, char *mimeType);

char* getMimeType_Advanced(const char *filename, char *mimeType);

#endif /* MIME_UTIL_H_ */
