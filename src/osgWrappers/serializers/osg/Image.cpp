#include <sstream>
#include <fstream>

#include <osg/Image>
#include <osgDB/ObjectWrapper>
#include <osgDB/InputStream>
#include <osgDB/OutputStream>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>
#include <osgDB/ConvertBase64>

static bool checkData( const osg::Image& image )
{
    return image.getDataPointer() != NULL;
}

static bool readData( osgDB::InputStream& is, osg::Image& image )
{
    bool readFromExternal = true;
    int decision = osgDB::IMAGE_EXTERNAL;
    is >> decision;

    switch ( decision )
    {
    case osgDB::IMAGE_INLINE_DATA:
        if ( is.isBinary() )
        {
            // _s, _t, _r
            int s, t, r;
            is >> s >> t >> r;

            // _data
            unsigned int size = is.readSize();

            if ( size )
            {                
                char* data = new char[size];
                if ( !data )
                    is.throwException( "InputStream::readImage() Out of memory." );                
                if ( is.getException() ) return false;

                is.readCharArray( data, size );
                image.setImage(s, t, r, image.getInternalTextureFormat(), image.getPixelFormat(), image.getDataType(),
                    (unsigned char*)data, osg::Image::USE_NEW_DELETE, image.getPacking() );
            }

            // _mipmapData
            unsigned int levelSize = is.readSize();
            osg::Image::MipmapDataType levels(levelSize);
            for ( unsigned int i=0; i<levelSize; ++i )
            {
                is >> levels[i];
            }
            if ( levelSize>0 )
                image.setMipmapLevels( levels );
            readFromExternal = false;
        } else { // ASCII            
            is >> is.BEGIN_BRACKET;

            // _s, _t, _r
            int s, t, r;
            is >> is.PROPERTY("Size") >> s >> t >> r;

            // mipDataLevel
            is >> is.PROPERTY("LevelSize");
            unsigned int levelSize = is.readSize()-1;           

            // _data
            std::vector<std::string> encodedData;
            encodedData.resize(levelSize+1);
            is.readWrappedString(encodedData.at(0));

            // Read all mipmap levels and to also add them to char* data
            // _mipmapData
            osg::Image::MipmapDataType levels(levelSize);
            for ( unsigned int i=1; i<=levelSize; ++i )
            {
                is.readWrappedString(encodedData.at(i));
            }

            osgDB::Base64decoder d;
            char* data = d.decode(encodedData, levels);
            // remove last item as we do not need the actual size
            // of the image including all mipmaps
            levels.pop_back();

            is >> is.END_BRACKET;

            if ( !data )
                is.throwException( "InputStream::readImage() Decoding of stream failed. Out of memory." );
            if ( is.getException() ) return false;

            image.setImage(s, t, r,
                image.getInternalTextureFormat(), image.getPixelFormat(), image.getDataType(),
                (unsigned char*)data, osg::Image::USE_NEW_DELETE, image.getPacking() );

            // Level positions (size of mipmap data)
            // from actual size of mipmap data read before
            if ( levelSize>0 )
                image.setMipmapLevels( levels );

            readFromExternal = false;
        }
        break;
    case osgDB::IMAGE_INLINE_FILE:
        if ( is.isBinary() )
        {
            unsigned int size = is.readSize();
            if ( size>0 )
            {
                std::vector<char> data(size);
                is.readCharArray( data.data(), data.size() );

                std::string ext = osgDB::getFileExtension( image.getFileName() );
                osgDB::ReaderWriter* reader =
                    osgDB::Registry::instance()->getReaderWriterForExtension( ext );
                if ( reader )
                {
                    std::stringstream inputStream;
                    inputStream.write( data.data(), data.size() );

                    osgDB::ReaderWriter::ReadResult rr = reader->readImage( inputStream );
                    if ( rr.validImage() )
                    {
                        osg::ref_ptr<osg::Image> tmp = rr.takeImage();

                        // copy content of inline file to image object
                        image.allocateImage(tmp->s(), tmp->t(), tmp->r(), tmp->getPixelFormat(), tmp->getDataType(), tmp->getPacking());
                        memcpy(image.data(), tmp->getDataPointer(), image.getTotalSizeInBytesIncludingMipmaps());
                    }
                    else
                    {
                        OSG_WARN << "InputStream::readImage(): "
                                               << rr.statusMessage() << std::endl;
                    }
                }
                else
                {
                    OSG_WARN << "InputStream::readImage(): Unable to find a plugin for "
                                           << ext << std::endl;
                }
            }
            readFromExternal = false;
        }
        break;
    case osgDB::IMAGE_EXTERNAL:
    case osgDB::IMAGE_WRITE_OUT:
        break;
    default:
        break;
    }

    if ( readFromExternal && !image.getFileName().empty() )
    {
        osgDB::ReaderWriter::ReadResult rr = osgDB::Registry::instance()->readImage(image.getFileName(), is.getOptions());
        if (rr.validImage())
        {
            osg::ref_ptr<osg::Image> tmp = rr.takeImage();

            // copy content of external file to image object
            image.allocateImage(tmp->s(), tmp->t(), tmp->r(), tmp->getPixelFormat(), tmp->getDataType(), tmp->getPacking());
            memcpy(image.data(), tmp->getDataPointer(), image.getTotalSizeInBytesIncludingMipmaps());
        }
        else
        {
           if (!rr.success()) OSG_WARN << "InputStream::readImage(): " << rr.statusMessage() << ", filename: " << image.getFileName() << std::endl;
        }
    }

    return true;
}

static bool writeData( osgDB::OutputStream& os, const osg::Image& image )
{
    int decision = osgDB::IMAGE_EXTERNAL;
    switch ( os.getWriteImageHint() )
    {
    case osgDB::OutputStream::WRITE_INLINE_DATA: decision = osgDB::IMAGE_INLINE_DATA; break;
    case osgDB::OutputStream::WRITE_INLINE_FILE: decision = osgDB::IMAGE_INLINE_FILE; break;
    case osgDB::OutputStream::WRITE_EXTERNAL_FILE: decision = osgDB::IMAGE_WRITE_OUT; break;
    case osgDB::OutputStream::WRITE_USE_EXTERNAL: decision = osgDB::IMAGE_EXTERNAL; break;
    default:
        if ( image.getWriteHint() == osg::Image::EXTERNAL_FILE )
            decision = osgDB::IMAGE_EXTERNAL;
        else if ( os.isBinary() )
            decision = osgDB::IMAGE_INLINE_DATA;
        break;
    }
    
    if ( decision==osgDB::IMAGE_WRITE_OUT || os.getWriteImageHint()==osgDB::OutputStream::WRITE_EXTERNAL_FILE )
    {
        std::string imageFileName = image.getFileName();
        if (imageFileName.empty())
        {
            OSG_WARN << "OutputStream::writeImage(): Failed to write because of empty Image::FileName " << std::endl;
            return false;
        }

        bool result = osgDB::writeImageFile( image, imageFileName );
        OSG_NOTICE << "OutputStream::writeImage(): Write image data to external file " << imageFileName << std::endl;
        if ( !result )
        {
            OSG_WARN << "OutputStream::writeImage(): Failed to write " << imageFileName << std::endl;
            return false;
        }
    }

    if ( os.getException() ) return false;

    os << decision;

    switch ( decision )
    {
    case osgDB::IMAGE_INLINE_DATA:
        if ( os.isBinary() )
        {
            // _s, _t, _r
            os << image.s() << image.t() << image.r();
            
            // _data
            os.writeSize( static_cast<unsigned int>(image.getTotalSizeInBytesIncludingMipmaps()) );

            for(osg::Image::DataIterator img_itr(&image); img_itr.valid(); ++img_itr)
            {
                os.writeCharArray( (char*)img_itr.data(), img_itr.size() );
            }

            // _mipmapData
            unsigned int numMipmaps = image.getNumMipmapLevels()-1;
            os.writeSize(numMipmaps);
            int s = image.s();
            int t = image.t();
            int r = image.r();
            unsigned int offset = 0;
            for (unsigned int i=0; i<numMipmaps; ++i)
            {
                unsigned int size = osg::Image::computeImageSizeInBytes(s,t,r,image.getPixelFormat(),image.getDataType(),image.getPacking());
                offset += size;

                os << offset;

                s >>= 1;
                t >>= 1;
                r >>= 1;
                if (s<1) s=1;
                if (t<1) t=1;
                if (r<1) r=1;
            }
        } else { // ASCII            
            os << os.BEGIN_BRACKET << std::endl;
            
            // _s, _t, _r            
            os << os.PROPERTY("Size") << image.s() << image.t() << image.r() << std::endl;

            // mipDataLevel
            os << os.PROPERTY("LevelSize") << image.getNumMipmapLevels() << std::endl;
            
            // _data
            osgDB::Base64encoder e;
            for(osg::Image::DataIterator img_itr(&image); img_itr.valid(); ++img_itr)
            {
                std::string encodedData;
                e.encode((char*)img_itr.data(), img_itr.size(), encodedData);
                // Each set of data is written into a separate string so we can
                // distiguish between main data and all mipmap levels, so writing
                // mipmap size is not required for ASCII mode.
                os.writeWrappedString(encodedData);
                os << std::endl;
            }

            os << os.END_BRACKET << std::endl;
        }
        break;
    case osgDB::IMAGE_INLINE_FILE:
        if ( os.isBinary() )
        {
            std::string fullPath = osgDB::findDataFile( image.getFileName() );
            osgDB::ifstream infile( fullPath.c_str(), std::ios::in|std::ios::binary );
            if ( infile )
            {
                infile.seekg( 0, std::ios::end );
                unsigned int size = infile.tellg();
                os.writeSize(size);

                if ( size>0 )
                {
                    std::vector<char> data(size);
                    
                    infile.seekg( 0, std::ios::beg );
                    infile.read( data.data(), data.size() );
                    os.writeCharArray( data.data(), data.size() );                    
                }
                infile.close();
            }
            else
            {
                OSG_WARN << "OutputStream::writeImage(): Failed to open image file "
                                    << image.getFileName() << std::endl;
                os << (unsigned int)0u;
            }
        } else { // ASCII
            os << std::endl;
        }
        break;    
    default:
        if ( !os.isBinary() ) // ASCII
        {
            os << std::endl;
        }
        break;
    }

    return true;
}

REGISTER_OBJECT_WRAPPER( Image,
                         new osg::Image,
                         osg::Image,
                         "osg::Object osg::Image" )
{
    {
        UPDATE_TO_VERSION_SCOPED( 112 )

        ADD_STRING_SERIALIZER(FileName, "");

        BEGIN_ENUM_SERIALIZER( WriteHint, NO_PREFERENCE );
            ADD_ENUM_VALUE( NO_PREFERENCE ) ;
            ADD_ENUM_VALUE( STORE_INLINE ) ;
            ADD_ENUM_VALUE( EXTERNAL_FILE );
        END_ENUM_SERIALIZER();

        BEGIN_ENUM_SERIALIZER( AllocationMode, USE_NEW_DELETE );
            ADD_ENUM_VALUE( NO_DELETE ) ;
            ADD_ENUM_VALUE( USE_NEW_DELETE );
            ADD_ENUM_VALUE( USE_MALLOC_FREE );
        END_ENUM_SERIALIZER();

        // Everything is done in OutputStream and InputStream classes
        ADD_GLENUM_SERIALIZER( InternalTextureFormat, GLint, GL_NONE );
        ADD_GLENUM_SERIALIZER( DataType, GLenum, GL_NONE );
        ADD_GLENUM_SERIALIZER( PixelFormat, GLenum, GL_NONE );
        ADD_INT_SERIALIZER( RowLength, 0 );
        ADD_UINT_SERIALIZER( Packing, 0 );

        BEGIN_ENUM_SERIALIZER( Origin, BOTTOM_LEFT );
            ADD_ENUM_VALUE( BOTTOM_LEFT ) ;
            ADD_ENUM_VALUE( TOP_LEFT );
        END_ENUM_SERIALIZER();
    }

    {
        UPDATE_TO_VERSION_SCOPED( 146 )

        ADD_USER_SERIALIZER( Data );
    }
}
