/*
 * Copyright (C) 2010 Stefano Sanfilippo
 *
 * smth-http.c : web transfer glue
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * \internal
 * \file   smth-http.c
 * \brief  Web transfer glue.
 * \author Stefano Sanfilippo
 * \date   12th June 2010
 */

#include <smth-http-defs.h>

error_t SMTH_fetch(Manifest *m)
{
	Fetcher f;
	int running_no = -1;

	initfetcher(&f);

	while (running_no)
	{
		/* Submit all transfers... */
		while (CURLM_CALL_MULTI_PERFORM == curl_multi_perform(f.handle, &running_no));
		if (running_no) resetfetcher(&f);
		execfetcher(&f);
	}

	disposefetcher(&f);

	return FETCHER_SUCCESS;
}

/*------------------------- HIC SUNT LEONES (CODICIS) ------------------------*/

/** The number of opened handles. */
static count_t handles = 0;

static error_t execfetcher(Fetcher *f)
{
	CURLMsg *msg;
	int queue;

	while ((msg = curl_multi_info_read(f->handle, &queue)))
	{
		if (msg->msg == CURLMSG_DONE)
		{
			CURL *e = msg->easy_handle;
			curl_multi_remove_handle(f->handle, e); //TODO invece di rimuoverlo, cambia url... reinit_handle o elimina
			curl_easy_cleanup(e);
		}
		else return FETCHER_TRANFER_FAILED; // will leak...
	}

	return FETCHER_SUCCESS;
}

static error_t initfetcher(Fetcher *f)
{
	count_t i;

	if (!handles && curl_global_init(CURL_GLOBAL_ALL))
		return FECTHER_FAILED_INIT; /* do it only once. */

	f->handle = curl_multi_init();
	if (!f->handle) return FECTHER_NO_MEMORY;

	/* limit the total amount of connections this multi handle uses */
	curl_multi_setopt(f->handle, CURLMOPT_MAXCONNECTS, FETCHER_MAX_TRANSFERS);

	for (i = 0; i < FETCHER_MAX_TRANSFERS; ++i)
	{
		CURL *eh = curl_easy_init();
		if (!eh) return FECTHER_NO_MEMORY;

		if (!reinithandle(eh)) return FETCHER_HANDLE_NOT_INITIALISED;

		/* Use the default write function */
		curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, cachefragment);
		/* Some servers don't like requests without a user-agent field... */
		curl_easy_setopt(eh, CURLOPT_USERAGENT, FETCHER_USERAGENT);
		/* No headers written, only body. */
		curl_easy_setopt(eh, CURLOPT_HEADER, 0L);
		/* No verbose messages. */
		curl_easy_setopt(eh, CURLOPT_VERBOSE, 0L);
		/* with old versions of libcurl: no progress meter */
		curl_easy_setopt(eh, CURLOPT_NOPROGRESS, 1L);

		if (curl_multi_add_handle(f->handle, eh))
		{   curl_easy_cleanup(eh);
			return FECTHER_HANDLE_NOT_ADDED;
		}
	}

	++handles;
	return FETCHER_SUCCESS;
}

static error_t disposefetcher(Fetcher *f)
{
	if (f->handle && curl_multi_cleanup(f->handle))
		return FETCHER_HANDLE_NOT_CLEANED;

	--handles;
	if (!handles) curl_global_cleanup();
}

static error_t resetfetcher(Fetcher *f)
{
	long sleep_time;
	int max_fd;
	fd_set read, write, except;
	struct timeval timeout;

	FD_ZERO(&read); FD_ZERO(&write); FD_ZERO(&except);

	if (curl_multi_fdset(f->handle, &read, &write, &except, &max_fd))
		return FETCHER_FAILED_FDSET;

	if (curl_multi_timeout(f->handle, &sleep_time))
		return FETCHER_CONNECTION_TIMEOUT;

	if (sleep_time == -1) sleep_time = 100;

	if (max_fd == -1)
	{	sleep(sleep_time / 1000); /* on Windows, Sleep(sleep_time); */
	}
	else
	{	timeout.tv_sec = sleep_time/1000;
		timeout.tv_usec = (sleep_time%1000)*1000;

		if (0 > select(max_fd+1, &read, &write, &except, &timeout))
			return FETCHER_NO_MULTIPLEX;
	}

	return FETCHER_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////

// char *mkdtemp(char *template);
// inizializza 10 handlers per connettersi al sito (o il numero che vuoi)
// con 6 urls da uno e 6 dall'altro...
// Una volta che l'handler ha finito, gli cambi l'url...
// una directory per ogni fetcher...
// per sicurezza, usiamo sempre e solo tmpfile....
// aggiungi trackno
// private data <- inserire con il file+timestamp+stream nella struct sopra...
// tmpfile();
//1. scarica il Manifest
//2. scopri quanto dura un frammento
//3. fai buffer a sufficienza
//4. scarica continuamente audio e video
//5. apri un folder temporaneo

/*	curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, cb);*/
/*	curl_easy_setopt(eh, CURLOPT_URL, url);*/
/*	curl_easy_setopt(eh, CURLOPT_PRIVATE, url);*/
/*				curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &url);*/

static size_t cachefragment(char *, size_t n, size_t l, void *p)
{
	FILE *test = fopen("/home/Stefano/Scrivania/test.html", "a");
	int op = fwrite(d, n, l, test);
	fclose(test);
	return op;
}

static bool reinithandle(CURL *eh)
{
	curl_easy_setopt(eh, CURLOPT_URL, "http://localhost:631"); //sempre lo stesso handle...
/*	curl_easy_setopt(f->handle, CURLOPT_WRITEDATA, test);*/
/*	curl_easy_setopt(eh, CURLINFO_PRIVATE, "http://localhost:631");*/
	return true;
}

#if 0
error_t SMTH_fetchmanifest();
error_t SMTH_fetchfragment();

error_t compilemanifesturl
error_t compilechunkurl

/* check for average download speed */
curl_easy_getinfo(curl_handle, CURLINFO_SPEED_DOWNLOAD, &val); //bytes/secondo double
#endif

////////////////////////////////////////////////////////////////////////////////

#if 0
$presentation   = "/path/$name.(ism|[\w]{1}[\w\d]*)";
$manifest       = "$presentation/Manifest"; //mettere i punti di domanda dopo

bitrate_t $bitrate; /* The bit rate of the Requested fragment. */
tick_t $time;       /* The time of the Requested fragment.     */

/* An Attribute of the Requested fragment used to disambiguate tracks. */
$attribute		= "$key=$value"
$key            = URISAFE_IDENTIFIER_NONNUMERIC;
$value          = URISAFE_IDENTIFIER;
/* The name of the requested stream */
$name           = URISAFE_IDENTIFIER_NONNUMERIC;
/* The type of response expected by the client. */
$noun           = (	"Fragments"    |  /* FragmentsNounFullResponse */
                    "FragmentInfo" |  /* FragmentsNounMetadataOnly */
                    "RawFragments" |  /* FragmentsNounDataOnly */
                    "KeyFrames"    ); /* FragmentsNounIndependentOnly */
$fragment       = "$presentation/QualityLevels($bitrate(,$attribute)*)/$noun($name=$time)";

/*  The SparseStreamPointer and related fields contain data required to locate
 *  the latest fragment of a sparse stream. This message is used in conjunction
 *  with a Fragment Response message.
 */

/* The timestamp of the latest timestamp for a fragment for the SparseStream
 * that occurs at the same point in time or earlier than the presentation
 * than the requested fragment.
 */
$timestamp = STRING_UINT64
/* The stream Name of the related Sparse Name. The value of this field MUST
 * match the Name field of the StreamElement field that describes the stream,
 * specified in section 2.2.2.3, in the Manifest Response.
 */
$name = CHARDATA
/* The latest fragment pointer for a single related sparse stream. */
$sparse = "$name=$timestamp"
/* The set of latest fragment pointer for all sparse streams related to a
 * single requested fragment.
 */
$sparseset = "$sparse(,$sparse)*"
$header = 1*CHAR
/* A set of data that indicates the latest fragment for all related sparse streams. */
$sparsepointer = "($header;)?ChildTrack=\"SparseStreamSet (; SparseStreamSet )*\""

/*  The Fragment Not Yet Available message is an HTTP Response with an empty
 *  message body field and the HTTP Status Code 412 Precondition Failed.
 */
standard = "QualityLevels({bitrate},{CustomAttributes})/Fragments(video={start_time})"
#endif 


/* vim: set ts=4 sw=4 tw=0: */
