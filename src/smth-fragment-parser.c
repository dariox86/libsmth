/*
 * Copyright (C) 2010 Stefano Sanfilippo
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
 * \file   smth-fragment-parser.c
 * \brief  Binary Fragments parser
 * \author Stefano Sanfilippo
 * \date   27th-30th June 2010
 */

#include <stdlib.h>
#include <stdio.h>
#include <endian.h>
#include <string.h>
#include <smth-fragment-defs.h>

/**
 * \brief        Parses a fragment opened as FILE and fills all the 
 *               details in a Fragment structure.
 * \param stream pointer to the stream from which to read the fragment.
 * \param f      pointer to the Fragment structure to be filled with data
 *               extracted from the stream.
 * \return       FRAGMENT_SUCCESS on successful parse, or an appropriate error
 *               code.
 */
error_t SMTH_parsefragment(Fragment *f, FILE *stream)
{   Box root;
	root.stream = stream;
	root.f = f;
	error_t result;

	memset(f, 0x00, sizeof (Fragment)); /* reset memory */
	SMTH_preparelist(&root.extlist);

//FIXME should work with `while`, but it does not... add multi box support...
//	while (!feof(root.stream))
	int i; for ( i = 0; i < 2; i++)
	{   
		result = parsebox(&root); //TODO aggiungere END_OF_FILE??
		if (result == FRAGMENT_SUCCESS)
		{
			switch (root.type)
			{	case MOOF: result = parsemoof(&root); break;
				case MDAT: result = parsemdat(&root); break;
				default: result = FRAGMENT_INAPPROPRIATE; break;
			}
			if (result != FRAGMENT_SUCCESS) break;
		}
		else break;
	}

	if (result != FRAGMENT_SUCCESS) 
	{   SMTH_disposefragment(root.f);
		return result;
	}

	/* if it is not EOF */
	if (feof(stream))
	{   SMTH_disposefragment(root.f);
		return FRAGMENT_BIGGER_THAN_DECLARED;
	}

	if (!SMTH_finalizelist(&root.extlist))
	{   SMTH_disposefragment(root.f);
		return FRAGMENT_NO_MEMORY;
	}

	root.f->extensions = (Extension **) root.extlist.list;

	return FRAGMENT_SUCCESS;
}

/**
 * \brief    Disposes properly of a Fragment. These days, it only calls
 *           \c free() on the dinamically allocated fields, but programmers
 *           are advised to use it instead of freeing memory by themselves,
 *           as the internal data structure may vary heavily in the future.
 * \param f  The fragment  to be destroyed.
 */
void SMTH_disposefragment(Fragment *f)
{
	int i;

	if (f->extensions)
	{
		for(i = 0; f->extensions[i]; i++)
		{   free(f->extensions[i]->data);
			free(f->extensions[i]);
		}
	}

	if (f->data) free(f->data);
	if (f->samples) free(f->samples);
	if (f->extensions) free(f->extensions);
	if (f->armor.vectors) free(f->armor.vectors);
	/* destroy even the reference */
	f->data = NULL;
	f->samples = NULL;
	f->extensions = NULL;
	f->armor.vectors = NULL;
}

/*------------------------- HIC SUNT LEONES (CODICIS) ------------------------*/

/**
 * \brief        Sets target reading an appropriate number of bytes from stream.
 *
 * If flag marked by mask is set, target is set reading sizeof (target) bytes
 * from the stream and decrements boxsize accordingly.
 * This macro is intended for internal use only, and implemention may vary
 * without notice.
 *
 * \param target The target to be set
 * \param mask   The mask to select the appropriate flag bit
 */
//XXX
#define GET_IF_FLAG_SET(target, mask) \
	if (boxflags & mask) \
	{   if (!readbox(&(target), sizeof (target), root)) \
			return FRAGMENT_IO_ERROR; \
		boxsize -= sizeof (target); \
	} \
	else target = 0;

/**
 * \brief      Read size bytes from root->stream, and stores them into dest.
 * \param dest Pointer to the destination buffer. Note that readbox will not
 *             check for buffer overflow.
 * \param size Number of bytes to read from the input stream
 * \param root Pointer to a box structure holding the stream pointer.
 * \return     true if no error was encountered, otherwise false
 */
static bool readbox(void *dest, size_t size, Box* root)
{
	return !((fread(dest, sizeof (byte_t), size, root->stream) < (size*sizeof (byte_t))) &&
	   (feof(root->stream) || ferror(root->stream)));
}

/**
 * \brief             Get flags & version field from the stream
 * \param defultflags Pointer to the buffer that will hold the flags
 * \return            true if the operation was successfull, otherwise false
 */
static bool getflags(flags_t *defaultflags, Box *root)
{	if (!readbox(defaultflags, sizeof (flags_t), root)) return false;
	*defaultflags = (flags_t) be32toh(*defaultflags); /* endian-safe */
	return true;
}

/**
 * \brief  If total size of extracted elements is smaller than Box size, it
 *         means that there is one or more UUIDBoxes awaiting at the end of
 *         the Box: try to parse them, and if they are not UUIDBoxes, return
 *         an error.
 * \return If buffer was overflowed (read size is bigger than Box size) returns
 *         FRAGMENT_OUT_OF_BOUNDS (it should never happen), else
 *         FRAGMENT_INAPPROPRIATE if an out-of-context Box was parsed, or
 *         or FRAGMENT_SUCCESS on successful parse.
 */
static error_t scanuuid(Box* root, signedlength_t boxsize)
{
	while (boxsize > 0)
	{   error_t result = parsebox(root);
		if (result != FRAGMENT_SUCCESS) return result;
		boxsize -= root->tsize;
		if (root->type != SPECIAL) return FRAGMENT_INAPPROPRIATE;
		result = parseuuid(root);
		if (result != FRAGMENT_SUCCESS) return result;
	}
	if (boxsize < 0) return FRAGMENT_OUT_OF_BOUNDS;
	return FRAGMENT_SUCCESS;
}

/**
 * \brief  prepares the first Box found on \c box->stream for parsing.
 *
 * parsebox will fill box structure with the size and the type of the incoming
 * block and position the input stream to the first byte of box data.
 * A parsing function would receive its size and type with the box structure,
 * then it may call parsebox to identify children Boxes and so on.
 * Obviously, it cannot be called by the parsing function itself, as the caller
 * needs to know in advance which parser invoke.
 *
 * \param  root the box to be prepared.
 * \return FRAGMENT_SUCCESS if the box was successfully prepared,
 *         FRAGMENT_IO_ERROR in case of read/write error and FRAGMENT_UNKNOWN 
 *         if an unknown Box type was encountered (it should never happen).
 */
static error_t parsebox(Box* root)
{
	length_t tmpsize;
	word_t name;
	BoxType element;
	shortlength_t offset = sizeof (shortlength_t) + sizeof (name);

	if (!readbox(&tmpsize, sizeof (shortlength_t), root)) return FRAGMENT_IO_ERROR;
	if (!getflags(&name, root)) return FRAGMENT_IO_ERROR;
	for (element = 0, root->type = UNKNOWN; element < UNKNOWN; element++)
	{
		if (name == BoxTypeID[element])
		{   root->type = element;
			break;
		}
	}
	/* if it is still unknown */
	if (root->type == UNKNOWN) return FRAGMENT_UNKNOWN;
	/* if it is a huge box */
	if (be32toh(tmpsize) == BOX_IS_HUGE)
	{
		if (!readbox(&root->bsize, sizeof (root->bsize), root)) return FRAGMENT_IO_ERROR;
		offset += sizeof (root->bsize);
		root->bsize = (signedlength_t) be64toh(root->bsize);
	}
	else root->bsize = (signedlength_t) be32toh(tmpsize);

	root->tsize = root->bsize;
	root->bsize -= offset;
	return FRAGMENT_SUCCESS;
}

/**
 * \brief      MoofBox (metadata container) parser
 * 
 * MoofBox has two compulsory children, one MfhdBox and one TrafBox,
 * and no fields. Hence, parsemoof will parse two Boxes, determine their type
 * and pass them to the appropriate parser.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, FRAGMENT_PARSE_ERROR if
 *             subparsers failed to decode Boxes, or FRAGMENT_INAPPROPRIATE if
 *             a Box that should not stay into a MoofBox was encountered.
 */
static error_t parsemoof(Box* root)
{
	signedlength_t boxsize = root->bsize;

	while (boxsize > 0)
	{   
		error_t result = parsebox(root);
		if (result == FRAGMENT_SUCCESS)
		{
			boxsize -= root->tsize; /* total size of the newly parsed Box */
			switch (root->type)
			{	case MFHD: result = parsemfhd(root); break;
				case TRAF: result = parsetraf(root); break;
				case SPECIAL: result = parseuuid(root); break;
				default: return FRAGMENT_INAPPROPRIATE;
			}
			if (result != FRAGMENT_SUCCESS) return result;
		}
		else return result;
	}
			
	if (boxsize < 0) return FRAGMENT_OUT_OF_BOUNDS;
	return FRAGMENT_SUCCESS;
}

/**
 * \brief      MfhdBox (position in the stream) parser
 *
 * Each MfhdBox contains a SequenceNumber field, representing the fragment's
 * position in the sequence for the Track. The SequenceNumber value for a
 * Fragment must be greater than for a Fragment earlier in the timeline, but
 * it is not required to be consecutive.
 *
 * \param  box pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or FRAGMENT_IO_ERROR.
 */

static error_t parsemfhd(Box* root)
{
	signedlength_t boxsize = root->bsize;
	count_t tmpsize;

	XXX_SKIP_4B_QUIRK;

	if (!readbox(&tmpsize, sizeof (tmpsize), root)) return FRAGMENT_IO_ERROR;
	root->f->index = (count_t) be32toh(tmpsize);
	boxsize -= sizeof (tmpsize);

	return scanuuid(root, boxsize);
}

/**
 * \brief      TrafBox (track-specific metadata) parser
 *
 * MoofBox has two compulsory children, one TfhdBox and one TrunBox,
 * and no fields. Hence, parsemoof will parse two Boxes, determine their type
 * and pass them to the appropriate parser.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or an appropriate error
 *             code.
 * \sa         parsemoof
 */
static error_t parsetraf(Box* root)
{
	error_t result;
	signedlength_t boxsize = root->bsize;

	while (boxsize > 0)
	{   
		result = parsebox(root);
		if (result == FRAGMENT_SUCCESS)
		{
			boxsize -= root->tsize; /* total size of the newly parsed Box */
			switch (root->type)
			{	case TFHD: result = parsetfhd(root); break;
				case TRUN: result = parsetrun(root); break;
				case SDTP: result = parsesdtp(root); break;
				case SPECIAL: result = parseuuid(root); break;
				default: return FRAGMENT_INAPPROPRIATE;
			}
			if (result != FRAGMENT_SUCCESS) return result;
		}
		else return result;
	}

	if (boxsize < 0) return FRAGMENT_OUT_OF_BOUNDS;
	return FRAGMENT_SUCCESS;
}

/**
 * \brief TfhdBox (per-sample defaults metadata) parser
 *
 * A Tfhd Box has no children and a variable number of default settings, whose
 * presence is specified by the TfhdBoxFlags bitfield, a 3*BYTE field heading
 * the Box.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or an appropriate error
 *             code.
 * \sa         parsetrun
 */
static error_t parsetfhd(Box* root)
{
	signedlength_t boxsize = root->bsize;
	flags_t boxflags;
	if (!getflags(&boxflags, root)) return FRAGMENT_IO_ERROR;
	boxsize -= sizeof (boxflags);

	uint64_t doubleword;
	uint32_t singleword;

	GET_IF_FLAG_SET(doubleword, TFHD_BASE_DATA_OFFSET_PRESENT);
	root->f->defaults.dataoffset = (offset_t) be64toh(doubleword);

	GET_IF_FLAG_SET(singleword, TFHD_SAMPLE_DESCRIPTION_INDEX_PRESENT);
	root->f->defaults.index = (count_t) be32toh(singleword);

	GET_IF_FLAG_SET(doubleword, TFHD_DEFAULT_SAMPLE_DURATION_PRESENT);
	root->f->defaults.duration = (tick_t) be64toh(doubleword);

	GET_IF_FLAG_SET(singleword, TFHD_DEFAULT_SAMPLE_SIZE_PRESENT);
	root->f->defaults.size = (bitrate_t) be32toh(singleword);

	GET_IF_FLAG_SET(singleword, TFHD_DEFAULT_SAMPLE_FLAGS_PRESENT);
	root->f->defaults.settings = (flags_t) be32toh(singleword);

	XXX_SKIP_4B_QUIRK;

	return scanuuid(root, boxsize);
}

/**
 * \brief TrunBox (per-sample metadata) parser
 *
 * A Trun Box has no children and a variable number of default settings, whose
 * presence is specified by the TrunBoxFlags bitfield, a 3*BYTE field heading
 * the Box.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or an appropriate error
 *             code.
 * \sa         parsetfhd
 * \sa		   GET_IF_FLAG_SET
 */
static error_t parsetrun(Box* root)
{
	signedlength_t boxsize = root->bsize;
	flags_t boxflags;
	Sample* tmp;

	if (!getflags(&boxflags, root)) return FRAGMENT_IO_ERROR;

	count_t samplecount;
	if (!readbox(&samplecount, sizeof (count_t), root))
		return FRAGMENT_IO_ERROR;
	root->f->sampleno = (count_t) be32toh(samplecount); /* endian-safe */

	boxsize -= sizeof (boxflags) + sizeof (samplecount);

        uint32_t singleword;
	GET_IF_FLAG_SET(singleword, TRUN_DATA_OFFSET_PRESENT);
        root->f->data_offset = (length_t) be32toh(singleword);
        GET_IF_FLAG_SET(singleword, TRUN_FIRST_SAMPLE_FLAGS_PRESENT);
        root->f->settings = (flags_t) be32toh(singleword);


	if(root->f->sampleno > 0)
	{	
		count_t i;
		tmp = calloc(root->f->sampleno, sizeof (Sample));
		if(!tmp) return FRAGMENT_NO_MEMORY;
		for( i = 0; i < root->f->sampleno; i++)
		{
			GET_IF_FLAG_SET(singleword, TRUN_SAMPLE_DURATION_PRESENT);
			tmp[i].duration = (bitrate_t) be32toh(singleword);
			GET_IF_FLAG_SET(singleword, TRUN_SAMPLE_SIZE_PRESENT);
			tmp[i].size = (bitrate_t) be32toh(singleword);
			GET_IF_FLAG_SET(singleword, TRUN_SAMPLE_FLAGS_PRESENT);
			tmp[i].settings = (flags_t) be32toh(singleword);
			GET_IF_FLAG_SET(singleword, TRUN_SAMPLE_COMPOSITION_TIME_OFFSET_PRESENT);
			tmp[i].timeoffset = (bitrate_t) be32toh(singleword);
		}
		root->f->samples = tmp;
	}

	error_t result = scanuuid(root, boxsize);
	if(result != FRAGMENT_SUCCESS)
	{   free(tmp);
		return result;
	}

	return FRAGMENT_SUCCESS;
}

/**
 * \brief      SdtpBox (Independent and Disposable Samples Box) parser
 *
 * Marks frames that can be intentionally dropped if the CPU cannot keep up.
 * This information is redundant, as it is repeated into more complete struct
 * TrunBox: the box will be simply skipped.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS
 */
static error_t parsesdtp(Box* root)
{
	/* 4B flags = 0 + 1B * samplesno (simpleflags) */
	fseek(root->stream, root->bsize, SEEK_CUR);
	return FRAGMENT_SUCCESS;
}

/**
 * \brief MdatBox (data container) parser
 *
 * A MdatBox has no children (not even a UUIDBox) and no fields: simply reads
 * it in Fragment::data. There may be only one per fragment, so we do not
 * worry about data concatenation.
 * Sample boundaries in the MdatBox are defined by the values of the
 * TrunBox::DefaultSampleSize and TrunBox::SampleSize fields. Individual sample
 * sizes are stored into SampleFields::size and the overall number of samples
 * in Fragment::sampleno.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or an appropriate error
 *             code.
 */
static error_t parsemdat(Box* root)
{
	byte_t *tmp = malloc(root->bsize);
	if (!tmp) return FRAGMENT_NO_MEMORY;
	if (!readbox(tmp, root->bsize, root))
	{   free(tmp);
		return FRAGMENT_IO_ERROR;
	}
	root->f->data = tmp;
	root->f->size = root->bsize;
	return FRAGMENT_SUCCESS;
}

/**
 * \brief  TfxdBox (absolute timestamp and duration) parser
 *
 * This field is found into a MoofBox (metadata) and should be ignored if it
 * appears in an on-demand presentation. It assumes that the signature has
 * already been stripped by a call to parseuuid.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or an appropriate error code.
 */
static error_t parsetfxd(Box* root)
{
	flags_t boxflags;
	signedlength_t boxsize = root->bsize;

	if (!getflags(&boxflags, root)) return FRAGMENT_IO_ERROR;
	boxsize -= sizeof(boxflags);
	//TODO semplificare
	if (boxflags & TFXD_LONG_FIELDS_MASK)
	{	
		tick_t tmp;
		if (!readbox(&tmp, sizeof (tick_t), root)) return FRAGMENT_IO_ERROR;
		root->f->timestamp = (tick_t) be64toh(tmp);
		if (!readbox(&tmp, sizeof (tick_t), root)) return FRAGMENT_IO_ERROR;
		root->f->duration  = (tick_t) be64toh(tmp);
		boxsize -= 2*sizeof (tick_t);
	}
	else
	{
		flags_t tmp;
		if (!readbox(&tmp, sizeof (flags_t), root)) return FRAGMENT_IO_ERROR;
		root->f->timestamp = (tick_t) be32toh(tmp);
		if (!readbox(&tmp, sizeof (flags_t), root)) return FRAGMENT_IO_ERROR;
		root->f->duration  = (tick_t) be32toh(tmp);
		boxsize -= 2*sizeof (flags_t);
	}

	return scanuuid(root, boxsize); /* sounds strange, but a uuid box may have
									 * a child uuid box... */
}

/**
 * \brief SampleEncryptionBox (content protection metadata) parser
 *
 * A SampleEncryptionBox is a particularly crafted VendorUUIDBox, with no
 * compulsory children and a few optional fields, whose presence is determined
 * using a flag field. It assumes that the signature has already been stripped
 * by a call to parseuuid.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or an appropriate error
 *             code.
 * \sa         encryptionuuid, parseuuid
 */
static error_t parseencr(Box* root)
{
	EncryptionType enc;
	signedlength_t boxsize = root->bsize;
	flags_t boxflags; /* first used to retrieve box flags, then encryption flags */
	if (!getflags(&boxflags, root)) return FRAGMENT_IO_ERROR;

	if (boxflags & ENCR_SAMPLE_ENCRYPTION_BOX_OPTIONAL_FIELDS_PRESENT)
	{
		if (!getflags(&boxflags, root)) return FRAGMENT_IO_ERROR;

		flags_t keytype = (flags_t) (boxflags & ENCRYPTION_KEY_TYPE_MASK);
		for (enc = NONE, root->f->armor.type = UNSET; enc < UNSET; enc++)
		{	if (!(keytype ^ EncryptionTypeID[enc])) /* match only if are equal */
			{   root->f->armor.type = enc;
				break;
			}
		}
		/* If it is still unknown */
		if (root->f->armor.type == UNSET) return FRAGMENT_UNKNOWN_ENCRYPTION;

		root->f->armor.vectorsize = (byte_t)(boxflags & ENCRYPTION_KEY_SIZE_MASK);

		if (!readbox(&root->f->armor.id, sizeof(uuid_t), root))
			return FRAGMENT_IO_ERROR;
		/* WARNING: if you change type size, it will break!! */
		boxsize -= sizeof (flags_t) + sizeof (byte_t) + sizeof (uuid_t); 
	}

	if (!readbox(&root->f->armor.vectorno, sizeof(count_t), root))
		return FRAGMENT_IO_ERROR;

	length_t vectorlength = root->f->armor.vectorsize * root->f->armor.vectorno;
	byte_t *tmp = malloc(vectorlength);
	if (!tmp) return FRAGMENT_NO_MEMORY;
	if (!readbox(tmp, vectorlength, root))
	{   free(tmp);
		return FRAGMENT_IO_ERROR;
	}

	boxsize -= vectorlength;

	error_t result = scanuuid(root, boxsize);
	if(result != FRAGMENT_SUCCESS)
	{   free(tmp);
		return result;
	}

	root->f->armor.vectors = tmp;

	return FRAGMENT_SUCCESS;
}

/**
 * \brief VendorExtensionUUIDBox (variable content) box.
 *
 * Parses a 16byte UUID and a bytestream content to a Variable structure, and
 * adds it to the list dereferenced by Fragment::extensions. If the Box is a 
 * SampleEncryptionBox, it calls parseencr.
 *
 * \param root pointer to the Box structure to be parsed
 * \return     FRAGMENT_SUCCESS on successful parse, or an appropriate error
 *             code.
 * \sa         parseencr
 */
static error_t parseuuid(Box* root)
{
	uuid_t uuid;

	if (!readbox(uuid, sizeof (uuid_t), root)) return FRAGMENT_IO_ERROR;
	root->bsize -= sizeof (uuid_t);

	/* If it is a SampleEncryptionBox */
	if (!memcmp(uuid, encryptionuuid, sizeof (uuid_t))) return parseencr(root);
	/* If it is a TfxdBox */
	if (!memcmp(uuid, tfxduuid, sizeof (uuid_t))) return parsetfxd(root);
	/* If it is a TfrfBox */
//	if (!memcmp(uuid, tfrfuuid, sizeof (uuid_t))) return parsetfrf(root); FIXME
	/* If it is an ordinary UUIDBox   */
	Extension *tmp = malloc(sizeof (Extension));
	if (!tmp) return FRAGMENT_NO_MEMORY;
	/* Data size */
	tmp->size = (length_t) root->bsize;
	if(!memcpy(uuid, tmp->uuid, sizeof(uuid_t)))
	{   free(tmp);
		return FRAGMENT_IO_ERROR;
	}
	/* Data body */
	byte_t *tmpdata = malloc(tmp->size);
	if (!readbox(tmpdata, tmp->size, root))
	{   free(tmp);
		free(tmpdata);
		return FRAGMENT_IO_ERROR;
	}
	tmp->data = tmpdata;

	if (!SMTH_addtolist(tmp, &root->extlist))
	{
		free(tmp);
		free(tmpdata);
		SMTH_disposelist(&root->extlist);
		return FRAGMENT_NO_MEMORY;
	}

	return FRAGMENT_SUCCESS;
}

/* vim: set ts=4 sw=4 tw=0: */
