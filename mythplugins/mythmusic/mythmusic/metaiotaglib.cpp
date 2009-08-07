
// Mythmusic
#include "metaiotaglib.h"
#include "metadata.h"

// Std
#include <cmath>

// Libmyth
#include <mythverbose.h>

// Taglib
#include <audioproperties.h>
#include <tag.h>
#include <tstring.h>

/* Redefine the TString conversion macros */
#undef QStringToTString
#define QStringToTString(s) TagLib::String(s.toUtf8().data(), TagLib::String::UTF8)
#undef TStringToQString
#define TStringToQString(s) QString::fromUtf8(s.toCString(true))


MetaIOTagLib::MetaIOTagLib()
    : MetaIO()
{
}

MetaIOTagLib::~MetaIOTagLib(void)
{
}

/*!
* \brief Writes metadata common to all tag formats to the tag
*
* \param tag A pointer to the tag
* \param metadata Pointer to the metadata
*/
void MetaIOTagLib::WriteGenericMetadata(Tag *tag, Metadata *metadata)
{
    if (!tag || !metadata)
        return;

    if (!metadata->Artist().isEmpty())
        tag->setArtist(QStringToTString(metadata->Artist()));
    
    if (!metadata->Title().isEmpty())
        tag->setTitle(QStringToTString(metadata->Title()));
    
    if (!metadata->Album().isEmpty())
        tag->setAlbum(QStringToTString(metadata->Album()));
    
    if (metadata->Year() > 999 && metadata->Year() < 10000) // 4 digit year.
        tag->setYear(metadata->Year());
    
    if (!metadata->Genre().isEmpty())
        tag->setGenre(QStringToTString(metadata->Genre()));
    
    if (0 != metadata->Track())
        tag->setTrack(metadata->Track());
}

/*!
* \brief Writes metadata common to all tag formats to the tag
*
* \param tag A pointer to the tag
* \param metadata Pointer to the metadata
*/
void MetaIOTagLib::ReadGenericMetadata(Tag *tag, Metadata *metadata)
{
    // Basic Tags
    if (metadata && tag && !tag->isEmpty())
    {
        metadata->setTitle(TStringToQString(tag->title()).trimmed());
        metadata->setArtist(TStringToQString(tag->artist()).trimmed());
        metadata->setAlbum(TStringToQString(tag->album()).trimmed());
        metadata->setTrack(tag->track());
        metadata->setYear(tag->year());
        metadata->setGenre(TStringToQString(tag->genre()).trimmed());
    }

    // Fallback to filename reading
    if (metadata->Title().isEmpty())
        readFromFilename(metadata);

    // If we don't have title and artist or don't have the length return NULL
    if (metadata->Title().isEmpty() && metadata->Artist().isEmpty())
    {
        VERBOSE(VB_IMPORTANT, QString("MetaIOTagLib: Failed to read metadata from '%1'")
        .arg(metadata->Filename()));
    }
}

/*!
* \brief Find the length of the track (in seconds)
*
* \param file Pointer to file object
* \returns An integer (signed!) to represent the length in seconds.
*/
int MetaIOTagLib::getTrackLength(TagLib::FileRef *file)
{
    int seconds = 0;

    if (file && file->audioProperties())
        seconds = file->audioProperties()->length();
    
    return seconds;
}

/*!
* \brief Find the length of the track (in seconds)
*
* \param filename The filename for which we want to find the length.
* \returns An integer (signed!) to represent the length in seconds.
*/
int MetaIOTagLib::getTrackLength(QString filename)
{
    int seconds = 0;
    QByteArray fname = filename.toLocal8Bit();
    TagLib::FileRef *file = new TagLib::FileRef(fname.constData());
    
    seconds = getTrackLength(file);

    // If we didn't get a valid length, add the metadata but show warning.
    if (seconds <= 0)
        VERBOSE(VB_GENERAL, QString("MetaIOTagLib: Failed to read length "
        "from '%1'. It may be corrupt.").arg(filename));
    
    return seconds;
}
