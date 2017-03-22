#include <osg/TextureBuffer>
#include <osg/Array>
#include <osgDB/ObjectWrapper>
#include <osgDB/InputStream>
#include <osgDB/OutputStream>

static bool checkBufferData( const osg::TextureBuffer& texture )
{
    return texture.getBufferData() != NULL;
}

static bool readBufferData( osgDB::InputStream& is, osg::TextureBuffer& texture )
{
    if ( is.isBinary() )
    {
        osg::ref_ptr<osg::BufferData> bufferData = is.readObjectOfType<osg::BufferData>();
        texture.setBufferData(bufferData);
    }
    else
    {
        is >> is.BEGIN_BRACKET;
        osg::ref_ptr<osg::BufferData> bufferData = is.readObjectOfType<osg::BufferData>();
        texture.setBufferData(bufferData);
        is >> is.END_BRACKET;    
    }
    
    return true;
}

static bool writeBufferData( osgDB::OutputStream& os, const osg::TextureBuffer& texture )
{    
    const osg::BufferData* bufferData = texture.getBufferData();

    if ( os.isBinary() )
    {
        os.writeObject(bufferData);
    }
    else
    {        
        os << os.BEGIN_BRACKET << std::endl;
        os.writeObject(bufferData);
        os << os.END_BRACKET << std::endl;
    }    

    return true;
}

REGISTER_OBJECT_WRAPPER( TextureBuffer,
                         new osg::TextureBuffer,
                         osg::TextureBuffer,
                         "osg::Object osg::StateAttribute osg::Texture osg::TextureBuffer" )
{
    ADD_USER_SERIALIZER( BufferData );       // _bufferData
    ADD_INT_SERIALIZER( TextureWidth, 0 ); // _textureWidth    
}
