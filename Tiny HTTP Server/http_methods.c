/*
 * methods.c
 *
 * Functions that implement HTTP methods, including
 * GET, HEAD, PUT, POST, and DELETE.
 *
 *  @since 2019-04-10
 *  @author: Philip Gust
 */

#include "http_methods.h"

#include <stddef.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#include "http_server.h"
#include "http_util.h"
#include "time_util.h"
#include "mime_util.h"
#include "properties.h"
#include "file_util.h"
#include "map.h"


/**
 * Create the directory listing
 *
 * @param filePath the path
 * @param uri the request URI
 * @return the FILE pointer of the tmp file for the listing page.
 */
static FILE *sendPageForDirectory(const char* filePath, const char* uri) {
	DIR *dir = opendir(filePath);
	if (dir == NULL) {
		return NULL;
	}

	struct dirent *entry;
	FILE *tmp = tmpfile();

	fprintf(tmp, "<html>\n");
	fprintf(tmp, "<head>\n");
	fprintf(tmp, "<title>index of %s</title>\n", uri);
	fprintf(tmp, "</head>\n");
	fprintf(tmp, "<body>\n");
	fprintf(tmp, "<h1>Index of %s</h1>\n", uri);
	fprintf(tmp, "<table>\n");

	// Header for directory contents.
	fprintf(tmp, "<tr>\n");
	fprintf(tmp, "\t<th valign=\"top\"></th>\n");
	fprintf(tmp, "\t<th>Name</th>\n");
	fprintf(tmp, "\t<th>Last modified</th>\n");
	fprintf(tmp, "\t<th>Size</th>\n");
	fprintf(tmp, "\t<th>Description</th>\n");
	fprintf(tmp, "</tr>\n");

	fprintf(tmp, "<tr>\n");
	fprintf(tmp, "\t<td colspan=\"5\"><hr /></td>\n");
	fprintf(tmp, "</tr>\n");

	while ((entry = readdir(dir)) != NULL) {
		struct stat s;
		char fileName[strlen(filePath)+strlen(entry->d_name)+2];
		char fileUri[strlen(uri)+strlen(entry->d_name)+2];

		makeFilePath(filePath, entry->d_name, fileName);
		makeFilePath(uri, entry->d_name, fileUri);
		if (stat(fileName, &s) != 0) {
			continue;
		}

		char timeBuf[MAXBUF];
		// Skip the current directory
		if (strcmp(entry->d_name, ".") == 0) {
			continue;
		} else if (strcmp(entry->d_name, "..") == 0) {
			// Skip the parent directory if we are at root
			if (strcmp(uri, "/") != 0) {
				fprintf(tmp, "<tr>\n");
				fprintf(tmp, "\t<td>&#x23ce;</td>\n");
				fprintf(tmp, "\t<td><a href=\"../\">Parent Directory</a></td>\n");
				fprintf(tmp, "\t<td align=\"right\">%s</td>\n",
					milliTimeToShortHM_Date_Time(s.st_mtim.tv_sec, timeBuf));
				fprintf(tmp, "\t<td align=\"right\">%lu</td>\n", (size_t)s.st_size);
				fprintf(tmp, "\t<td></td>\n");
				fprintf(tmp, "</tr>\n");
			}
		}else{
			fprintf(tmp, "<tr>\n");
			fprintf(tmp, "\t<td>%s</td>\n", S_ISDIR(s.st_mode) ? "&#x1F4C1;" : "");
			fprintf(tmp, "\t<td><a href=\"%s\">%s</td>\n", fileUri, entry->d_name);
			fprintf(tmp, "\t<td align=\"right\">%s</td>\n",
				milliTimeToShortHM_Date_Time(s.st_mtim.tv_sec, timeBuf));
			fprintf(tmp, "\t<td align=\"right\">%lu</td>\n", (size_t)s.st_size);
			fprintf(tmp, "\t<td></td>\n");
			fprintf(tmp, "</tr>\n");
		}
	}

	fprintf(tmp, "<tr>\n");
	fprintf(tmp, "\t<td colspan=\"5\"><hr /></td>\n");
	fprintf(tmp, "</tr>\n");

	fprintf(tmp, "</table>\n");
	fprintf(tmp, "</body>\n");
	fprintf(tmp, "</html>\n");

	// Make sure all the content gets written to disk.
	fflush(tmp);
	rewind(tmp);

	closedir(dir);
	return tmp;
}

/**
 * Handle GET or HEAD request.
 *
 * @param the socket stream
 * @param uri the request URI
 * @param requestHeaders the request headers
 * @param responseHeaders the response headers
 * @param sendContent send content (GET)
 */
static void do_get_or_head(FILE *stream, const char *uri, Properties *requestHeaders, Properties *responseHeaders, bool sendContent) {
	// get path to URI in file system
	char filePath[MAXBUF];
	resolveUri(uri, filePath);

	// ensure file exists
	struct stat sb;
	if (stat(filePath, &sb) != 0) {
		sendErrorResponse(stream, 404, "Not Found", responseHeaders);
		return;
	}
	// ensure file is a regular file or a directory
	if (!S_ISREG(sb.st_mode) && !S_ISDIR(sb.st_mode)) {
		sendErrorResponse(stream, 404, "Not Found", responseHeaders);
		return;
	}

	// record the file length
	size_t contentLen = 0;
	char buf[MAXBUF];
	FILE* contentStream = NULL;

	// Handle directory listing
	if (S_ISDIR(sb.st_mode)){
		// generate HTML page for the
		contentStream = sendPageForDirectory(filePath, uri);
		if (contentStream == NULL){
			sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
			return;
		}
		struct stat contentStat;
		if (fileStat(contentStream, &contentStat) != 0){
			sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
			return;
		}
		contentLen = (size_t)contentStat.st_size;
		sprintf(buf,"%lu", contentLen);
		putProperty(responseHeaders,"Content-Length", buf);

		putProperty(responseHeaders, "Content-Type", "text/html");
		putProperty(responseHeaders, "Last-Modified",
						milliTimeToRFC_1123_Date_Time(sb.st_mtim.tv_sec, buf));
	}else{
		contentLen = (size_t)sb.st_size;
		sprintf(buf,"%lu", contentLen);
		putProperty(responseHeaders,"Content-Length", buf);

		// record the last-modified date/time
		time_t timer = sb.st_mtim.tv_sec;
		putProperty(responseHeaders,"Last-Modified",
					milliTimeToRFC_1123_Date_Time(timer, buf));

		// get mime type of file
//		getMimeType(filePath, buf);
		getMimeType_Advanced(filePath, buf);
//		printf("mime type: %s\n", buf);
		putProperty(responseHeaders, "Content-type", buf);

		if (sendContent) {
			contentStream = fopen(filePath, "r");
			if (contentStream == NULL){
				sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
				return;
			}
		}
	}

	// send response
	sendResponseStatus(stream, 200, "OK");

	// Send response headers
	sendResponseHeaders(stream, responseHeaders);

	if (sendContent) {  // for GET
		copyFileStreamBytes(contentStream, stream, contentLen);
	}
	fclose(contentStream);
}
//do head and get are almost the same.
/**
 * Handle GET request.
 *
 * @param the socket stream
 * @param uri the request URI
 * @param requestHeaders the request headers
 * @param responseHeaders the response headers
 * @param headOnly only perform head operation
 */
void do_get(FILE *stream, const char *uri, Properties *requestHeaders, Properties *responseHeaders) {
	do_get_or_head(stream, uri, requestHeaders, responseHeaders, true);
}

/**
 * Handle HEAD request.
 *
 * @param the socket stream
 * @param uri the request URI
 * @param requestHeaders the request headers
 * @param responseHeaders the response headers
 */
void do_head(FILE *stream, const char *uri, Properties *requestHeaders, Properties *responseHeaders) {
	do_get_or_head(stream, uri, requestHeaders, responseHeaders, false);
}

/**
 * Handle PUT request.
 *
 * @param the socket stream
 * @param uri the request URI
 * @param requestHeaders the request headers
 * @param responseHeaders the response headers
 */
void do_put(FILE *stream, const char *uri, Properties *requestHeaders, Properties *responseHeaders) {
	//get the request file path
	char filePath[MAXBUF];
	resolveUri(uri, filePath);

	//create any intermediate dirs
	char path[MAXBUF];
	if (getPath(filePath, path) == NULL){
		sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
		return;
	}
	if(mkdirs(path,0755) != 0){
		sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
		return;
	}

	// determine if file exist
	struct stat sb;
	//creat = 0 if file already exist, -1 if file doesn't exist
	int created = stat(filePath, &sb);
	FILE* fptr = fopen(filePath, "w");

	if (fptr == NULL){
		sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
		return;
	}

	//get stream file size
	char buf[MAXBUF];
	findProperty(requestHeaders, 0, "Content-Length", buf);
	int file_size = strtol(buf, NULL, 10);

	//rewrite the content
	if(copyFileStreamBytes(stream, fptr, file_size) == 0){
		if(created){
			sendResponseStatus(stream, 201, "Created");
		}else{
			sendResponseStatus(stream, 200, "OK");
		}
	}else{
		sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
		return;
	}
	fclose(fptr);

	// Send response headers
	putProperty(responseHeaders, "Content-Length", "0");
	putProperty(responseHeaders, "Content-Type", "text/html");
	sendResponseHeaders(stream, responseHeaders);
}

/**
 * Handle POST request.
 *
 * @param the socket stream
 * @param uri the request URI
 * @param requestHeaders the request headers
 * @param responseHeaders the response headers
 */
//usually use in form data
void do_post(FILE *stream, const char *uri, Properties *requestHeaders, Properties *responseHeaders) {
	//get the request file path
	char filePath[MAXBUF];
	resolveUri(uri, filePath);

	struct stat sb;
	if (fileStat(stream, &sb) != 0) {
		sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
		return;
	}
	//get stream file size
	char buf[MAXBUF];
	findProperty(requestHeaders, 0, "Content-Length", buf);
	int size = strtol(buf, NULL, 10);

	//create a new file
	FILE *new_file = fopen(filePath, "w");
	if(new_file == NULL){
		sendErrorResponse(stream, 500, "Internal Server Error", responseHeaders);
		return;
	}
	copyFileStreamBytes(stream, new_file, size);
	fclose(new_file);

	// Send response status
	sendResponseStatus(stream, 200, "OK");

	// Send response headers
	putProperty(responseHeaders, "Content-Length", "0");
	putProperty(responseHeaders, "Content-Type", "text/html");
	sendResponseHeaders(stream, responseHeaders);
}

/**
 * Handle DELETE request.
 *
 * @param the socket stream
 * @param uri the request URI
 * @param requestHeaders the request headers
 * @param responseHeaders the response headers
 */
void do_delete(FILE *stream, const char *uri, Properties *requestHeaders, Properties *responseHeaders) {
	// get path to URI in file system
	char filePath[MAXBUF];
	resolveUri(uri, filePath);

	// ensure file exists
	struct stat sb;
	if (stat(filePath, &sb) != 0) {
		sendErrorResponse(stream, 404, "Not Found", responseHeaders);
		return;
	}

	//1.if is a directory
	if (!S_ISREG(sb.st_mode)) {
		//delete the empty directory. if the directory is not empty send error
		if(rmdir(filePath) != 0){
			sendErrorResponse(stream, 405, "Method not Allowed", responseHeaders);
			return;
		}else{
		//successful
			sendResponseStatus(stream, 200, "OK");
		}
	//2.if is a file
	}else{
		//remove the file. if fails, send error
		if(unlink(filePath) != 0){
			sendErrorResponse(stream, 404, "Not Found", responseHeaders);
			return;
		}else{
			//successful
			sendResponseStatus(stream, 200, "OK");
		}
	}
	// Send response headers
	putProperty(responseHeaders, "Content-Length", "0");
	putProperty(responseHeaders, "Content-Type", "text/html");

	sendResponseHeaders(stream, responseHeaders);
}
