/*
 * mime_util.c
 *
 * Functions for processing MIME types.
 *
 *  @since 2019-04-10
 *  @author: Philip Gust
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "mime_util.h"
#include "http_server.h"
#include "map.h"

static const char *DEFAULT_MIME_TYPE = "application/octet-stream";

void buildMap(FILE* mime, map_base_t *mime_map){
	char line[MAXBUF];
	for(int i = 0; i < 13; i++){
		fgets(line, MAXBUF, mime);
	}
//	while (fgets(line, MAXBUF, mime) != NULL) {
//		printf("comment line: %s\n", line);
//	        if (strcmp(&line[0], "#") != 0 || strcmp(&line[0], "\n") != 0){
//	        	break;
//	        }
//	}
	while(fgets(line, MAXBUF, mime) != NULL){
		char* token;
		char* _line = strdup(line);
		token = strtok(_line, "\t\n");
		char* value = token;
		token = strtok(NULL, "\t");
		while(token != NULL){
			char* token_key = strtok(token, " ");
			while(token_key != NULL){
				char *pos;
				if ((pos = strchr(token_key, '\n'))!= NULL){
					*pos = '\0';
				}
				if(map_set_(mime_map, token_key, value, strlen(value))< 0){
					fprintf(stderr, "Unable to add mime types to map\n");
				}
//				printf("token: %s, value: %s\n", token_key, value);
				token_key = strtok(NULL, " ");
			}
			token = strtok(NULL, "\t");
		}
	}
}
/**
 * Lowercase a string
 */
char *strlower(char *s)
{
    for (char *p = s; *p != '\0'; p++) {
        *p = tolower(*p);
    }

    return s;
}

char* getMimeType_Advanced(const char *filename, char *mimeType)
{
	// special-case directory based on trailing '/'
	if (filename[strlen(filename)-1] == '/') {
		strcpy(mimeType, "text/directory");
		return mimeType;
	}

	// find file extension
    char *p = strrchr(filename, '.');
    if (p == NULL) { // default if no extension
    	strcpy(mimeType, DEFAULT_MIME_TYPE);
    	return mimeType;
    }

    // lower-case extension
    char ext[MAXBUF];
    strcpy(ext, ++p);
    strlower(ext);

//    const char *mtstr;

	const char* mtstr = (char*)map_get_(&mime_map, ext);
	if (mtstr == NULL){
		mtstr = DEFAULT_MIME_TYPE;
	}
    strcpy(mimeType, mtstr);
    return mimeType;
}
/**
 * Return a MIME type for a given filename.
 *
 * @param filename the name of the file
 * @param mimeType output buffer for mime type
 * @return pointer to mime type string
 */
char *getMimeType(const char *filename, char *mimeType)
{
	// special-case directory based on trailing '/'
	if (filename[strlen(filename)-1] == '/') {
		strcpy(mimeType, "text/directory");
		return mimeType;
	}

	// find file extension
    char *p = strrchr(filename, '.');
    if (p == NULL) { // default if no extension
    	strcpy(mimeType, DEFAULT_MIME_TYPE);
    	return mimeType;
    }

    // lower-case extension
    char ext[MAXBUF];
    strcpy(ext, ++p);
    strlower(ext);

    const char *mtstr;

    // hash on first char?
    switch (*ext) {
    case 'c':
        if (strcmp(ext, "css") == 0) { mtstr = "text/css"; }
        break;
    case 'g':
        if (strcmp(ext, "gif") == 0) { mtstr = "image/gif"; }
        break;
    case 'h':
        if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) { mtstr = "text/html"; }
        break;
    case 'j':
    	if (strcmp(ext, "jpeg") == 0 || strcmp(ext, "jpg") == 0) { mtstr = "image/jpg"; }
    	else if (strcmp(ext, "js") == 0) { mtstr = "application/javascript"; }
    	else if (strcmp(ext, "json") == 0) { mtstr = "application/json"; }
    	break;
    case 'p':
        if (strcmp(ext, "png") == 0) { mtstr = "image/png"; }
        break;
    case 't':
    	if (strcmp(ext, "txt") == 0) { mtstr = "text/plain"; }
    	break;
    default:
    	mtstr = DEFAULT_MIME_TYPE;
    }

    strcpy(mimeType, mtstr);
    return mimeType;
}
