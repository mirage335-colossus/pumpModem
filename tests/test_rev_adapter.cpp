#define DATAPUMP_REV_ADAPTER_TEST
#include <glew/glew.h>
#include "gui_extension_fixture.hpp"
#include "document_geometry_fixture.hpp"
import Rev.Graphics.FrameBuffer;
#include "../src/gui/backend_rev.cpp"
#ifdef _WIN32
int main(int argc,char** argv) {return datapump_rev_main(argc,argv);}
#endif
