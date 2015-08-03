#include "framework.h"
#include "tbb/tbb.h"

using namespace prototyper;

framework frm;

struct voxel
{
  unsigned int color; //RGBA8 alpha==0 means empty
};

voxel voxels[32*32*32];

void set_voxel( const ivec3& p, voxel v )
{
  assert( p.z >= 0 && p.z < 32 && p.y >= 0 && p.y < 32 && p.x >= 0 && p.x < 32 );
  unsigned address = p.z * 32 * 32 + p.y * 32 + p.x;
  voxels[address] = v;
}

//supersampling positions
vec3 pos00 = vec3( 0.25, 0.25, 0 );
vec3 pos10 = vec3( 0.75, 0.25, 0 );
vec3 pos01 = vec3( 0.25, 0.75, 0 );
vec3 pos11 = vec3( 0.75, 0.75, 0 );

struct camera
{
	vec3 pos, view, up, right;
};

//////////////////////////////////////////////////
// Code that runs the raytracer
//////////////////////////////////////////////////

uvec2 screen( 0 );
bool fullscreen = false;
bool silent = false;
string title = "Voxel rendering stuff";

vec3 rotate_2d( const vec3& pp, float angle )
{
  vec3 p = pp;
	p.x = p.x * cos( angle ) - p.y * sin( angle );
	p.y = p.y * cos( angle ) + p.x * sin( angle );
	return p;
}

void calculate_ssaa_pos()
{
	float angle = atan( 0.5 );
	float stretch = sqrt( 5.0 ) * 0.5;

	 pos00 = rotate_2d(  pos00, angle );
	 pos01 = rotate_2d(  pos01, angle );
	 pos10 = rotate_2d(  pos10, angle );
	 pos11 = rotate_2d(  pos11, angle );

	 pos00 = (  pos00 - vec3( 0.5, 0.5, 0 ) ) * stretch + vec3( 0.5, 0.5, 0 );
	 pos01 = (  pos01 - vec3( 0.5, 0.5, 0 ) ) * stretch + vec3( 0.5, 0.5, 0 );
	 pos10 = (  pos10 - vec3( 0.5, 0.5, 0 ) ) * stretch + vec3( 0.5, 0.5, 0 );
	 pos11 = (  pos11 - vec3( 0.5, 0.5, 0 ) ) * stretch + vec3( 0.5, 0.5, 0 );
}

::camera lookat( const vec3& eye, const vec3& lookat, const vec3& up )
{
  ::camera c;
	c.view = normalize( lookat - eye );
	c.up = normalize( up );
	c.pos = eye;
	c.right = normalize( cross( c.view, c.up ) );
	c.up = normalize( cross( c.right, c.view ) );
	return c;
}

int main( int argc, char** argv )
{
  shape::set_up_intersection();

  map<string, string> args;

  for( int c = 1; c < argc; ++c )
  {
    args[argv[c]] = c + 1 < argc ? argv[c + 1] : "";
    ++c;
  }

  cout << "Arguments: " << endl;
  for_each( args.begin(), args.end(), []( pair<string, string> p )
  {
    cout << p.first << " " << p.second << endl;
  } );

  /*
   * Process program arguments
   */

  stringstream ss;
  ss.str( args["--screenx"] );
  ss >> screen.x;
  ss.clear();
  ss.str( args["--screeny"] );
  ss >> screen.y;
  ss.clear();

  if( screen.x == 0 )
  {
    screen.x = 1920;
  }

  if( screen.y == 0 )
  {
    screen.y = 1080;
  }

  try
  {
    args.at( "--fullscreen" );
    fullscreen = true;
  }
  catch( ... ) {}

  try
  {
    args.at( "--help" );
    cout << title << ", written by Marton Tamas." << endl <<
         "Usage: --silent      //don't display FPS info in the terminal" << endl <<
         "       --screenx num //set screen width (default:1280)" << endl <<
         "       --screeny num //set screen height (default:720)" << endl <<
         "       --fullscreen  //set fullscreen, windowed by default" << endl <<
         "       --help        //display this information" << endl;
    return 0;
  }
  catch( ... ) {}

  try
  {
    args.at( "--silent" );
    silent = true;
  }
  catch( ... ) {}

  /*
   * Initialize the OpenGL context
   */

  frm.init( screen, title, fullscreen );
  frm.set_vsync( true );

  //set opengl settings
  glEnable( GL_DEPTH_TEST );
  glDepthFunc( GL_LEQUAL );
  glFrontFace( GL_CCW );
  glEnable( GL_CULL_FACE );
  glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
  glClearDepth( 1.0f );

  frm.get_opengl_error();

  glViewport( 0, 0, screen.x, screen.y );

  GLuint ss_quad = frm.create_quad( vec3(-1, -1, 0), vec3(1, -1, 0), vec3(-1, 1, 0), vec3(1, 1, 0) );

  GLuint dda_shader = 0;
  frm.load_shader( dda_shader, GL_VERTEX_SHADER, "../shaders/voxel/dda/dda.vs" );
  frm.load_shader( dda_shader, GL_FRAGMENT_SHADER, "../shaders/voxel/dda/dda.ps" );

  /*
   * Set up the scene
   */

  vec2 resolution = vec2( screen.x, screen.y );
  float aspect =  resolution.y /  resolution.x;
  vec3 world_size = vec3( 32 );
  vec3 voxel_size = vec3( 1 );

  //set up the camera
	::camera cam = lookat( vec3( 10, 5, 24 ), vec3( 16, 16, 16 ), vec3( 0, 1, 0 ) );

  //initialize thread building blocks
  tbb::task_scheduler_init();

  calculate_ssaa_pos();

  vec2 inv_resolution = 1.0f /  resolution;

  int size = 32;

  for( int z = 0; z < size; ++z )
    for( int y = 0; y < size; ++y )
      for( int x = 0; x < size; ++x )
      {
        unsigned color = 0xffffff << 8;
        
        /**/
        //sphere fill
        aabb current = aabb(vec3(x, y, z) + 0.5, 0.5);
        sphere s = sphere( vec3( 16, 16, 16 ), 10 );

        bool i = current.is_intersecting(&s);
        color |= (0xff * i);
        /**/

        //random fill
        //color |= (0xff * (frm.get_random_num( 0, 1 ) > 0.5f));
        
         voxel v;
        v.color = color;
         set_voxel(ivec3(x, y, z), v);
      }

  GLuint voxel_data_ssbo;
  glGenBuffers( 1, &voxel_data_ssbo );
  glBindBuffer( GL_SHADER_STORAGE_BUFFER, voxel_data_ssbo );
  glBufferData( GL_SHADER_STORAGE_BUFFER, sizeof(voxels), 0, GL_DYNAMIC_DRAW );
  glBufferSubData( GL_SHADER_STORAGE_BUFFER, 0, sizeof(voxels), &voxels[0] );

  glUseProgram( dda_shader );
  unsigned block_index = glGetProgramResourceIndex( dda_shader, GL_SHADER_STORAGE_BLOCK, "voxel_data" );
  glShaderStorageBlockBinding(dda_shader, block_index, 0);

  /*
   * Handle events
   */

  vec4 mouse = vec4(0);

  auto event_handler = [&]( const sf::Event & ev )
  {
    switch( ev.type )
    {
      case sf::Event::MouseMoved:
        {
           mouse.x = ev.mouseMove.x;
           mouse.y = screen.y - ev.mouseMove.y;
        }
      default:
        break;
    }
  };

  /*
   * Render
   */

  sf::Clock timer;
  timer.restart();

  sf::Clock movement_timer;
  movement_timer.restart();

  float move_amount = 10;
  float orig_move_amount = move_amount;

  vec3 movement_speed = vec3(0);

  cout << "Init finished, rendering starts..." << endl;

  frm.display( [&]
  {
    frm.handle_events( event_handler );

    float seconds = movement_timer.getElapsedTime().asMilliseconds() / 1000.0f;

    if( sf::Keyboard::isKeyPressed( sf::Keyboard::LShift ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RShift ) )
    {
      move_amount = orig_move_amount * 3.0f;
    }
    else
    {
      move_amount = orig_move_amount;
    }

    if( seconds > 0.01667 )
    {
      //move camera
      if( sf::Keyboard::isKeyPressed( sf::Keyboard::A ) )
      {
        movement_speed.x -= move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::D ) )
      {
        movement_speed.x += move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::W ) )
      {
        movement_speed.y += move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::S ) )
      {
        movement_speed.y -= move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::Q ) )
      {
        movement_speed.z -= move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::E ) )
      {
        movement_speed.z += move_amount;
      }

      {
         cam.pos += vec3(0,0,-1) * movement_speed.y * seconds;
         cam.pos += vec3(-1,0,0) * movement_speed.x * seconds;
         cam.pos += vec3(0,1,0) * movement_speed.z * seconds;

        //set up the camera
	       cam = lookat(  cam.pos, vec3( 16, 16, 16 ), vec3( 0, 1, 0 ) );
      }

      movement_speed *= 0.5;

      movement_timer.restart();
    }

    float time = timer.getElapsedTime().asMilliseconds() * 0.001f;

    glDisable( GL_DEPTH_TEST );
    glUseProgram( dda_shader );

    glUniform1f( glGetUniformLocation( dda_shader, "time" ), time );
    glUniform4fv( glGetUniformLocation( dda_shader, "mouse" ), 1, &mouse.x );
    glUniform2fv( glGetUniformLocation( dda_shader, "resolution" ), 1, &resolution.x );
    glUniform2fv( glGetUniformLocation( dda_shader, "inv_resolution" ), 1, &inv_resolution.x );
    glUniform1f( glGetUniformLocation( dda_shader, "aspect" ), aspect );
    glUniform3fv( glGetUniformLocation( dda_shader, "world_size" ), 1, &world_size.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "voxel_size" ), 1, &voxel_size.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "cam.pos" ), 1, &cam.pos.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "cam.view" ), 1, &cam.view.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "cam.up" ), 1, &cam.up.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "cam.right" ), 1, &cam.right.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "pos00" ), 1, &pos00.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "pos01" ), 1, &pos01.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "pos10" ), 1, &pos10.x );
    glUniform3fv( glGetUniformLocation( dda_shader, "pos11" ), 1, &pos11.x );

    glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 0, voxel_data_ssbo );
    
    glBindVertexArray( ss_quad );
    glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0 );

    glEnable( GL_DEPTH_TEST );

    //cout << "frame" << endl;

    frm.get_opengl_error();
  }, silent );

  return 0;
}
