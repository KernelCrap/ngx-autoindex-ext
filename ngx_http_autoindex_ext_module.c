/*
* ngx_http_autoindex_ext_module.c
* Written by Casper Jørgensen
*
* Simple autoindex module that outputs a table of directories and files.
* Can be styled with an external stylesheet.
*
* Based on the stock nginx autoindex module by Igor Sysoev.
*
*/ 

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static ngx_int_t ngx_http_autoindex_ext_init(ngx_conf_t *cf);
static char * ngx_http_autoindex_ext_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static void * ngx_http_autoindex_ext_create_loc_conf(ngx_conf_t *cf);

typedef struct {
	ngx_flag_t	enabled;
	ngx_flag_t	exact_size;
	ngx_str_t	stylesheet;
} ngx_http_autoindex_ext_loc_conf_t;

static ngx_command_t ngx_http_autoindex_ext_commands[] = {
	{
		ngx_string("autoindex_ext"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
		ngx_conf_set_flag_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_autoindex_ext_loc_conf_t, enabled), NULL
	},
	{
		ngx_string("autoindex_ext_exact_size"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
		ngx_conf_set_flag_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_autoindex_ext_loc_conf_t, exact_size), NULL
	},
	{
		ngx_string("autoindex_ext_stylesheet"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_autoindex_ext_loc_conf_t, stylesheet), NULL
	},
	ngx_null_command
};

static ngx_http_module_t ngx_http_autoindex_ext_module_ctx = {
	NULL, ngx_http_autoindex_ext_init,
	NULL, NULL, NULL, NULL,
	ngx_http_autoindex_ext_create_loc_conf,
	ngx_http_autoindex_ext_merge_loc_conf
};

ngx_module_t ngx_http_autoindex_ext_module = {
	NGX_MODULE_V1,
	&ngx_http_autoindex_ext_module_ctx,
	ngx_http_autoindex_ext_commands,
	NGX_HTTP_MODULE,
	NULL, NULL, NULL,
	NULL, NULL, NULL,
	NULL, NGX_MODULE_V1_PADDING
};

typedef struct {
	ngx_str_t		name;
	ngx_uint_t		is_dir;
	time_t			date;
	off_t			size;
	size_t			escape;
} ngx_http_autoindex_ext_entry_t;

static u_char ngx_http_autoindex_ext_header1[] =
"<!doctype html>"					CRLF
"<html lang=\"en\">"				CRLF
"<head>"							CRLF
"\t<meta charset=\"utf-8\" />"		CRLF
;
static u_char ngx_http_autoindex_ext_header2[] =
"</head>"							CRLF
"<body>"							CRLF
"<table>"							CRLF
"\t<thead>"							CRLF
"\t\t<tr>"							CRLF
"\t\t\t<th>Name</th>"				CRLF
"\t\t\t<th>Size</th>"				CRLF
"\t\t</tr>"							CRLF
"\t</thead>"						CRLF
"\t<tbody>"							CRLF
;
static u_char ngx_http_autoindex_ext_footer[] =
"\t</tbody>"						CRLF
"</table>"							CRLF
"</body>"							CRLF
"</html>"
;
static u_char ngx_http_autoindex_ext_back[] =
"\t\t<tr>"												CRLF
"\t\t\t<td><a href=\"../\">Parent directory/</a></td>"	CRLF
"\t\t\t<td>-</td>"										CRLF
"\t\t</tr>"												CRLF
;

static int ngx_libc_cdecl
ngx_http_autoindex_ext_cmp_entries(const void *one, const void *two)
{
	ngx_http_autoindex_ext_entry_t *first = (ngx_http_autoindex_ext_entry_t *) one;
	ngx_http_autoindex_ext_entry_t *second = (ngx_http_autoindex_ext_entry_t *) two;

	if (first->is_dir && !second->is_dir)
		return -1;

	if (!first->is_dir && second->is_dir)
		return 1;

	return (int) ngx_strcmp(first->name.data, second->name.data);
}

static ngx_int_t
ngx_http_autoindex_ext_number_length(off_t number)
{
	off_t s = number;
	ngx_int_t size_chars = 0;
	while(s) {
		size_chars++;
		s /= 10;
	}
	return size_chars;
}

static ngx_int_t
ngx_http_autoindex_ext_handler(ngx_http_request_t *r)
{
	ngx_int_t							rc;
	ngx_buf_t							*b;
	ngx_chain_t							out;
	ngx_http_autoindex_ext_loc_conf_t	*conf;
	ngx_array_t							entries;
	ngx_http_autoindex_ext_entry_t		*entry;
	u_char								*last, *filename, scale;
	ngx_str_t							path;
	ngx_dir_t							dir;
	off_t								s;
	size_t								i, response_size, length, root, size;

	// First, we need to have access to the config.
	conf = ngx_http_get_module_loc_conf(r, ngx_http_autoindex_ext_module);
	if (!conf)
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	
	// Make sure indexing is enabled.
	if (!conf->enabled)
		return NGX_DECLINED;
	
	// Only handle folders (this will allow files to be served).
	if (r->uri.data[r->uri.len - 1] != '/')
		return NGX_DECLINED;

	// This module only handles GET and HEAD requests.
	if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD)))
		return NGX_HTTP_NOT_ALLOWED;

	// Discard request body, since we don't need it at this point.
	rc = ngx_http_discard_request_body(r);
	if (rc != NGX_OK)
		return rc;

	// Set the headers (the response is HTML).
	r->headers_out.content_type.len = sizeof("text/html") - 1;
	r->headers_out.content_type.data = (u_char *)"text/html";

	// If the request type is HEAD, send the header now.
	if (r->method == NGX_HTTP_HEAD) {
		r->headers_out.status = NGX_HTTP_OK; 
		return ngx_http_send_header(r);
	}

	// Initialize the array that will contain file entries.
	if (ngx_array_init(&entries, r->pool, 128, sizeof(ngx_http_autoindex_ext_entry_t)) != NGX_OK)
		return NGX_HTTP_INTERNAL_SERVER_ERROR;

	// Map the URI to a path.
	last = ngx_http_map_uri_to_path(r, &path, &root, 255);
	if (last == NULL)
		return NGX_HTTP_INTERNAL_SERVER_ERROR;

	// Open the path for reading.
	if (ngx_open_dir(&path, &dir) == NGX_ERROR)
		return NGX_HTTP_INTERNAL_SERVER_ERROR;

	// Allocate memory for the filename (4096 is the max path length).
	if ((filename = ngx_palloc(r->pool, 4096)) == NULL)
		return NGX_HTTP_INTERNAL_SERVER_ERROR;

	// Loop through all files.
	for(;;)
	{
		ngx_set_errno(0);

		// Read the directory, break when there are no more files.
		if (ngx_read_dir(&dir) == NGX_ERROR) {
			if (ngx_errno != NGX_ENOMOREFILES)
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
			break;
		}

		// Skip hidden files and folders.
		if (ngx_de_name(&dir)[0] == '.')
			continue;

		// Get the length of the name.
		length = ngx_de_namelen(&dir);

		// Get additional file info.
		if (!dir.valid_info) {
			// Add the path to the filename and a /.
			last = ngx_cpystrn(filename, path.data, path.len + 1);
			*last++ = '/';

			// Copy the actual filename into the path.
			ngx_cpystrn(last, ngx_de_name(&dir), length + 1);		

			// Get additional information about the file.
			if (ngx_de_info(filename, &dir) == NGX_FILE_ERROR) {
				if (ngx_errno != NGX_ENOENT)
					continue;
				if (ngx_de_link_info(filename, &dir) == NGX_FILE_ERROR)
					return NGX_HTTP_INTERNAL_SERVER_ERROR;
			}
		}

		// Push an entry into the array.
		if ((entry = ngx_array_push(&entries)) == NULL)
			return NGX_HTTP_INTERNAL_SERVER_ERROR;

		// Allocate memory for the file name.
		entry->name.len			= length;
		entry->name.data		= ngx_palloc(r->pool, length + 1);

		// Make sure we have allocated memory.
		if (entry->name.data == NULL)
			return NGX_HTTP_INTERNAL_SERVER_ERROR;

		// Assign file name.
		ngx_cpystrn(entry->name.data, ngx_de_name(&dir), length + 1);

		// Assign file attributes.
		entry->is_dir			= ngx_de_is_dir(&dir);
		entry->date				= ngx_de_mtime(&dir);
		entry->size				= ngx_de_size(&dir);
		entry->escape			= 2 * ngx_escape_uri(NULL, ngx_de_name(&dir), length, NGX_ESCAPE_URI_COMPONENT);
	}
	
	// Close the directory.
	if (ngx_close_dir(&dir) == NGX_ERROR)
		return NGX_HTTP_INTERNAL_SERVER_ERROR;

	// We need to calculate the size for the buffer.
	response_size = 0;

	// Header and footer added.
	response_size += sizeof(ngx_http_autoindex_ext_header1) - 1;
	response_size += sizeof(ngx_http_autoindex_ext_header2) - 1;
	response_size += sizeof(ngx_http_autoindex_ext_footer) - 1;

	// We dont need the link to the parent directory in the root dir.
	if (r->uri.len != 1)
		response_size += sizeof(ngx_http_autoindex_ext_back) - 1;

	// Title and stylesheet.
	response_size += sizeof("\t<title>Index of </title>" CRLF) - 1;
	response_size += r->uri.len;
	if (conf->stylesheet.len > 1) {
		response_size += conf->stylesheet.len;
		response_size += sizeof("\t<link rel=\"stylesheet\" type=\"text/css\" href=\"\">" CRLF) - 1;
	}
	
	// Add all files, along with the HTML table tags.
	entry = entries.elts;
	for (i = 0; i < entries.nelts; i++)
	{
		response_size += entry[i].name.len + entry[i].escape;
		response_size += entry[i].name.len;
		response_size += sizeof("\t\t<tr>" CRLF
		"\t\t\t<td><a href=\"\"></a></td>" CRLF
		"\t\t\t<td></td>" CRLF
		"\t\t</tr>" CRLF) - 1;
		
		if (entry[i].is_dir)
			response_size += 3; // 1 for - and 2 for / twice.
		else
		{
			if (conf->exact_size) {
				// Calculate the size of the integer.
				response_size += ngx_http_autoindex_ext_number_length(entry[i].size);
			} else {
				s = entry[i].size;
				if (s > 1024 * 1024 - 1) {
					size = (ngx_int_t) (s / (1024 * 1024));
					if ((s % (1024 * 1024)) > (1024 * 1024 / 2 - 1)) {
						size++;
					}
					scale = 'M';
				} else if (s > 9999) {
					size = (ngx_int_t) (s / 1024);
					if (s % 1024 > 511) {
						size++;
					}
					scale = 'K';
				} else {
					size = (ngx_int_t)s;
					scale = '\0';
				}
				if (scale) {
					response_size += 1; // scale
					response_size += ngx_http_autoindex_ext_number_length(size);
				} else {
					response_size += ngx_http_autoindex_ext_number_length(size);
				}				
			}
		}
	}

	// Sort the entries.
	if (entries.nelts > 1) {
		ngx_qsort(entry, (size_t)entries.nelts,
		sizeof(ngx_http_autoindex_ext_entry_t),
		ngx_http_autoindex_ext_cmp_entries);
	}

	// Allocate a buffer for the response body based on the size we calculated.
	b = ngx_create_temp_buf(r->pool, response_size);
	if (b == NULL)
	return NGX_HTTP_INTERNAL_SERVER_ERROR;
	
	// Attach this buffer to the buffer chain.
	out.buf = b;
	out.next = NULL;

	// Start adding data to the response, this is pretty messy.
	b->last = ngx_cpymem(b->last, ngx_http_autoindex_ext_header1, sizeof(ngx_http_autoindex_ext_header1) - 1);
	b->last = ngx_cpymem(b->last, "\t<title>Index of ", sizeof("\t<title>Index of ") - 1);
	b->last = ngx_cpymem(b->last, r->uri.data, r->uri.len);
	b->last = ngx_cpymem(b->last, "</title>" CRLF, sizeof("</title>" CRLF) - 1);
	if (conf->stylesheet.len > 1) {
		b->last = ngx_cpymem(b->last, "\t<link rel=\"stylesheet\" type=\"text/css\" href=\"", sizeof("\t<link rel=\"stylesheet\" type=\"text/css\" href=\"") - 1);
		b->last = ngx_cpymem(b->last, conf->stylesheet.data, conf->stylesheet.len);
		b->last = ngx_cpymem(b->last, "\">" CRLF, sizeof("\">" CRLF) - 1);
	}	
	b->last = ngx_cpymem(b->last, ngx_http_autoindex_ext_header2, sizeof(ngx_http_autoindex_ext_header2) - 1);

	// Only have the parent directory link in sub directories.
	if (r->uri.len != 1)
		b->last = ngx_cpymem(b->last, ngx_http_autoindex_ext_back, sizeof(ngx_http_autoindex_ext_back) - 1);

	// Add all the entries to the table.
	entry = entries.elts;
	for (i = 0; i < entries.nelts; i++)
	{
		b->last = ngx_cpymem(b->last, "\t\t<tr>" CRLF "\t\t\t<td>", sizeof("\t\t<tr>" CRLF "\t\t\t<td>") - 1);
		b->last = ngx_cpymem(b->last, "<a href=\"", sizeof("<a href=\"") - 1);

		if (entry[i].escape) {
            ngx_escape_uri(b->last, entry[i].name.data, entry[i].name.len, NGX_ESCAPE_URI_COMPONENT);
            b->last += entry[i].name.len + entry[i].escape;
        } else {
            b->last = ngx_cpymem(b->last, entry[i].name.data, entry[i].name.len);
        }
		if (entry[i].is_dir)
			b->last = ngx_cpymem(b->last, "/", sizeof("/") - 1);
		b->last = ngx_cpymem(b->last, "\">", sizeof("\">") - 1);
		b->last = ngx_cpymem(b->last, entry[i].name.data, entry[i].name.len);
		if (entry[i].is_dir)
			b->last = ngx_cpymem(b->last, "/", sizeof("/") - 1);
		b->last = ngx_cpymem(b->last, "</a>", sizeof("</a>") - 1);
		b->last = ngx_cpymem(b->last, "</td>" CRLF "\t\t\t<td>", sizeof("</td>" CRLF "\t\t\t<td>") - 1);
		if (entry[i].is_dir)
			b->last = ngx_cpymem(b->last, "-", sizeof("-") - 1);
		else
		{
			if (conf->exact_size)
			b->last = ngx_sprintf(b->last, "%i", entry[i].size);
			else
			{
				s = entry[i].size;
				if (s > 1024 * 1024 - 1) {
					size = (ngx_int_t) (s / (1024 * 1024));
					if ((s % (1024 * 1024)) > (1024 * 1024 / 2 - 1)) {
						size++;
					}
					scale = 'M';
				} else if (s > 9999) {
					size = (ngx_int_t) (s / 1024);
					if (s % 1024 > 511) {
						size++;
					}
					scale = 'K';
				} else {
					size = (ngx_int_t)s;
					scale = '\0';
				}
				if (scale) {
					b->last = ngx_sprintf(b->last, "%i%c", size, scale);
				} else {
					b->last = ngx_sprintf(b->last, "%i", size);
				}
			}
			
		}
		b->last = ngx_cpymem(b->last, "</td>" CRLF, sizeof("</td>" CRLF) - 1);
		b->last = ngx_cpymem(b->last, "\t\t</tr>" CRLF, sizeof("\t\t</tr>" CRLF) - 1);
	}	
	
	// Finally, add the footer.
	b->last = ngx_cpymem(b->last, ngx_http_autoindex_ext_footer, sizeof(ngx_http_autoindex_ext_footer) - 1);

	// In memory and last buffer in chain.
	b->memory = 1;
	b->last_buf = 1;

	// Set the headers.
	r->headers_out.status = NGX_HTTP_OK;
	r->headers_out.content_length_n = response_size;

	// Send the headers of the response.
	rc = ngx_http_send_header(r);
	if (rc == NGX_ERROR || rc > NGX_OK || r->header_only)
		return rc;

	// Send the buffer chain of the response.
	return ngx_http_output_filter(r, &out);
}

static void *
ngx_http_autoindex_ext_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_autoindex_ext_loc_conf_t	*conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_autoindex_ext_loc_conf_t));
	if (conf == NULL)
		return NGX_CONF_ERROR;

	conf->enabled = NGX_CONF_UNSET;
	conf->exact_size = NGX_CONF_UNSET;

	return conf;
}

static char *
ngx_http_autoindex_ext_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
	ngx_http_autoindex_ext_loc_conf_t *prev = parent;
	ngx_http_autoindex_ext_loc_conf_t *conf = child;

	ngx_conf_merge_value(conf->enabled, prev->enabled, 0);

	return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_autoindex_ext_init(ngx_conf_t *cf)
{
	ngx_http_handler_pt        *h;
	ngx_http_core_main_conf_t  *cmcf;

	cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

	h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
	if (h == NULL)
		return NGX_ERROR;

	*h = ngx_http_autoindex_ext_handler;

	return NGX_OK;
}