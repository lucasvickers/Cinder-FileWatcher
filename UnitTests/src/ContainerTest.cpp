#include "cinder/app/Platform.h"
#include "cinder/app/App.h"
#include "cinder/Utilities.h"
#include "catch.hpp"

#include <chrono>

#include "utils.h"
#include "FileWatcher.h"

using namespace ci;
using namespace std;
using namespace ci::app;

void prepPath( fs::path path )
{
	fs::remove_all( path );
	CI_ASSERT( createTestingDir( path ) );
}

TEST_CASE( "BasicContainerTest" )
{
	SECTION( "A container is used to watch a combination of files and paths" )
	{
		fs::path root = getTestingPath();
		fs::remove_all( root );
		CI_ASSERT( createTestingDir( root ) );
		
		ActionMap actions;
		auto func = [ &actions ]( const ci::fs::path& file, filewatcher::EventType type ) {
			actions[file].process( type );
		};
		
		filewatcher::WatchedTargetMap<std::string> fileMap;
		
		// watch varied files and paths via container
		
	}
}
