#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class WatchUnitTestsApp : public App {
  public:
	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void update() override;
	void draw() override;
};

void WatchUnitTestsApp::setup()
{
}

void WatchUnitTestsApp::mouseDown( MouseEvent event )
{
}

void WatchUnitTestsApp::update()
{
}

void WatchUnitTestsApp::draw()
{
	gl::clear( Color( 0, 0, 0 ) ); 
}

CINDER_APP( WatchUnitTestsApp, RendererGl )
