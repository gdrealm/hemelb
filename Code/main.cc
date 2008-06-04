// In this file, the functions useful to initiate/end the LB simulation
// and perform the dynamics are reported

#include "config.h"


#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define MYPORT 65250
#define CONNECTION_BACKLOG 10


FILE *timings_ptr;
float globalLongitude = 0.;

void visUpdateLongitude (char *parameters_file_name, Net *net, Vis *vis)
{
  FILE *parameters_file;

  float par_to_send[14];
  float ctr_x, ctr_y, ctr_z;
  float longitude, latitude;
  float zoom;
  float density_max, velocity_max, stress_max;
  float dummy;
  
  if (net->id == 0)
    {
      parameters_file = fopen (parameters_file_name, "r");
      
      fscanf (parameters_file, "%e \n", &dummy);
      fscanf (parameters_file, "%e \n", &dummy);
      fscanf (parameters_file, "%e \n", &ctr_x);
      fscanf (parameters_file, "%e \n", &ctr_y);
      fscanf (parameters_file, "%e \n", &ctr_z);
      fscanf (parameters_file, "%e \n", &longitude);
      fscanf (parameters_file, "%e \n", &latitude);
      fscanf (parameters_file, "%e \n", &zoom);
      
      fscanf (parameters_file, "%i \n", &vis_image_freq);
      fscanf (parameters_file, "%i \n", &vis_flow_field_type);
      fscanf (parameters_file, "%i \n", &vis_mode);
      fscanf (parameters_file, "%e \n", &vis_absorption_factor);
      fscanf (parameters_file, "%e \n", &vis_cutoff);
      fscanf (parameters_file, "%e \n", &density_max);
      fscanf (parameters_file, "%e \n", &velocity_max);
      fscanf (parameters_file, "%e \n", &stress_max);

      fclose (parameters_file);
      
      par_to_send[  0 ] = ctr_x;
      par_to_send[  1 ] = ctr_y;
      par_to_send[  2 ] = ctr_z;
      par_to_send[  3 ] = longitude;
      par_to_send[  4 ] = latitude;
      par_to_send[  5 ] = zoom;
      par_to_send[  6 ] = 0.1 + (float)vis_image_freq;
      par_to_send[  7 ] = 0.1 + (float)vis_flow_field_type;
      par_to_send[  8 ] = 0.1 + (float)vis_mode;
      par_to_send[  9 ] = vis_absorption_factor;
      par_to_send[ 10 ] = vis_cutoff;
      par_to_send[ 11 ] = density_max;
      par_to_send[ 12 ] = velocity_max;
      par_to_send[ 13 ] = stress_max;
    }
#ifndef NOMPI
  net->err = MPI_Bcast (par_to_send, 14, MPI_FLOAT, 0, MPI_COMM_WORLD);
#endif
  
  ctr_x                  =      par_to_send[  0 ];
  ctr_y                  =      par_to_send[  1 ];
  ctr_z                  =      par_to_send[  2 ];
  longitude              =      par_to_send[  3 ];
  latitude               =      par_to_send[  4 ];
  zoom                   =      par_to_send[  5 ];
  vis_image_freq         = (int)par_to_send[  6 ];
  vis_flow_field_type    = (int)par_to_send[  7 ];
  vis_mode               = (int)par_to_send[  8 ];
  vis_absorption_factor  =      par_to_send[  9 ];
  vis_cutoff             =      par_to_send[ 10 ];
  density_max            =      par_to_send[ 11 ];
  velocity_max           =      par_to_send[ 12 ];
  stress_max             =      par_to_send[ 13 ];
  
  visProjection (0.5F * vis->system_size, 0.5F * vis->system_size,
		 PIXELS_X, PIXELS_Y,
		 ctr_x, ctr_y, ctr_z,
		 5.F * vis->system_size,
		 globalLongitude, latitude,
		 0.5F * (5.F * vis->system_size),
		 zoom);
  
  if (vis_flow_field_type == DENSITY)
    {
      vis_flow_field_value_max_inv = 1.F / density_max;
    }
  else if (vis_flow_field_type == VELOCITY)
    {
      vis_flow_field_value_max_inv = 1.F / velocity_max;
    }
  else
    {
      vis_flow_field_value_max_inv = 1.F / stress_max;
    }
}


char host_name[255];

// data per pixel are colour id and pixel id (2 * sizeof(int) bytes)
int data_per_pixel = 2;
int bytes_per_pixel_data = data_per_pixel * sizeof(int);

// one int for colour_id and one for pixel id
u_int pixel_data_bytes = IMAGE_SIZE * bytes_per_pixel_data;

// it is assumed that the frame size is the only detail
u_int frame_details_bytes = 1 * sizeof(int);

char *xdrSendBuffer_pixel_data;
char *xdrSendBuffer_frame_details;

int bits_per_char = sizeof(char) * 8;
int bits_per_two_chars = 2 * bits_per_char;


int recv_all (int sockid, char *buf, int *length)
{
  int received_bytes = 0;
  int bytes_left_to_receive = *length;
  int n;

  while (received_bytes < *length)
    {
      n = recv(sockid, buf+received_bytes, bytes_left_to_receive, 0);
      
      if (n == -1) break;
      
      received_bytes += n;
      bytes_left_to_receive -= n;
    }
  *length = received_bytes;
  
  return n == -1 ? -1 : 0;
}


int send_all(int sockid, char *buf, int *length ) {
  
  int sent_bytes = 0;
  int bytes_left_to_send = *length;
  int n;
	
  while( sent_bytes < *length ) {
    n = send(sockid, buf+sent_bytes, bytes_left_to_send, 0);
    if (n == -1)
      break;
    sent_bytes += n;
    bytes_left_to_send -= n;
  }
	
  *length = sent_bytes;
	
  return n==-1?-1:0;

}


void *hemeLB_steer (void *ptr)
{
  while(1) {

  long int read_fd = (long int)ptr;
  //printf("Kicking off steering thread with FD %i\n", (int)read_fd);
 
  int num_chars = sizeof(int) / sizeof(char);
  int bytes = sizeof(char) * num_chars;
 
  char* xdr_steering_data = (char *)malloc(bytes);
  
  XDR xdr_steering_stream;
  
  
  xdrmem_create(&xdr_steering_stream, xdr_steering_data, bytes, XDR_DECODE);
 
  recv_all (read_fd, xdr_steering_data, &num_chars);
  
  int view_type;
 
  xdr_int (&xdr_steering_stream, &view_type);
 
  //printf ("VIEW TYPE -> %i\n", view_type);
  
  vis_flow_field_type = view_type;
  }
}


void *hemeLB_network (void *ptr)
{
  gethostname (host_name, 255);

// #ifndef STEER
  FILE *f = fopen ("env_details.asc","w");
  
  fprintf (f, "%s\n", host_name);
  fclose (f);
  
  fprintf (timings_ptr, "MPI 0 Hostname -> %s\n\n", host_name);
// #endif
  
  int sock_fd;
  int new_fd;
  int yes = 1;
  
  int is_broken_pipe = 0;
  int frame_number = 0;
  
  int pixel_r, pixel_g, pixel_b;
  int pixel_i, pixel_j;
  int colour_id, pixel_id;
  
  ColPixel *col_pixel_p;
  
  signal(SIGPIPE, SIG_IGN); // Ignore a broken pipe 
  
  pthread_t steering_thread;
  pthread_attr_t steering_thread_attrib; 
  
  pthread_attr_init (&steering_thread_attrib);
  pthread_attr_setdetachstate (&steering_thread_attrib, PTHREAD_CREATE_JOINABLE);
  
  while (1)
    {
	
      pthread_mutex_lock ( &network_buffer_copy_lock );
	    
      struct sockaddr_in my_address;
      struct sockaddr_in their_addr; // client address
      
      socklen_t sin_size;
      
      if ((sock_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
	{
	  perror("socket");
	  exit (1);
	}
      
      if (setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	{
	  perror("setsockopt");
	  exit (1);
	}
      
      my_address.sin_family = AF_INET;
      my_address.sin_port = htons (MYPORT);
      my_address.sin_addr.s_addr = INADDR_ANY;
      memset (my_address.sin_zero, '\0', sizeof my_address.sin_zero);
      
      if (bind (sock_fd, (struct sockaddr *)&my_address, sizeof my_address) == -1)
	{
	  perror ("bind");
	  exit (1);
	}
      
      if (listen (sock_fd, CONNECTION_BACKLOG) == -1)
	{
	  perror ("listen");
	  exit (1);
	}
      
      sin_size = sizeof (their_addr);
      
      if ((new_fd = accept (sock_fd, (struct sockaddr *)&their_addr, &sin_size)) == -1)
	{
	  perror("accept");
	  continue;
	}
      
      fprintf (timings_ptr, "server: got connection from %s (FD %i)\n", inet_ntoa (their_addr.sin_addr), new_fd);
      printf ("RG thread: server: got connection from %s (FD %i)\n", inet_ntoa (their_addr.sin_addr), new_fd);
      
      pthread_create (&steering_thread, &steering_thread_attrib, hemeLB_steer, (void*)new_fd);	  
	  
      close(sock_fd);
      
      is_broken_pipe = 0;
      
      pthread_mutex_unlock ( &network_buffer_copy_lock );
      
      while (!is_broken_pipe)
	{
	  pthread_mutex_lock ( &network_buffer_copy_lock );
	  pthread_cond_wait (&network_send_frame, &network_buffer_copy_lock);
	  
	  int bytesSent = 0;
	  
	  XDR xdr_network_stream_frame_details;
	  XDR xdr_network_stream_pixel_data;
	  
	  xdrmem_create (&xdr_network_stream_pixel_data, xdrSendBuffer_pixel_data,
			 pixel_data_bytes, XDR_ENCODE);
	  
	  xdrmem_create (&xdr_network_stream_frame_details, xdrSendBuffer_frame_details,
			 frame_details_bytes, XDR_ENCODE);
	  
	  for (int i = 0; i < col_pixels; i++)
	    {
	      //col_pixel_p = &col_pixel_locked[ i ];
	      col_pixel_p = &col_pixel_recv[ i ];
	      
	      pixel_r = max(0, min(255, (int)col_pixel_p->r));
	      pixel_g = max(0, min(255, (int)col_pixel_p->g));
	      pixel_b = max(0, min(255, (int)col_pixel_p->b));
	      
	      pixel_i = PixelI (col_pixel_p->i);
	      pixel_j = PixelJ (col_pixel_p->i);
	      
	      colour_id = (pixel_r << bits_per_two_chars) + (pixel_g << bits_per_char) + pixel_b;
	      pixel_id = (pixel_i << bits_per_two_chars) + pixel_j;
	      
	      xdr_int (&xdr_network_stream_pixel_data, &colour_id);
	      xdr_int (&xdr_network_stream_pixel_data, &pixel_id);
	    }
	  
	  int frameBytes = xdr_getpos(&xdr_network_stream_pixel_data);
	  
	  xdr_int (&xdr_network_stream_frame_details, &frameBytes);
	  
	  int detailsBytes = xdr_getpos(&xdr_network_stream_frame_details);
	  
	  int ret = send_all(new_fd, xdrSendBuffer_frame_details, &detailsBytes);
	  
          if (ret < 0) {
            is_broken_pipe = 1;
            break;
          } else {
            bytesSent += detailsBytes;
          }
	  
	  ret = send_all(new_fd, xdrSendBuffer_pixel_data, &frameBytes);
	  
          if (ret < 0) {
		    printf("RG thread: broken network pipe...\n");
            is_broken_pipe = 1;
			pthread_mutex_unlock ( &network_buffer_copy_lock );
            break;
          } else {
            bytesSent += frameBytes;
          }
	  
	  //fprintf (timings_ptr, "bytes sent %i\n", bytesSent);
	  printf ("RG thread: bytes sent %i\n", bytesSent);
	  
	  xdr_destroy (&xdr_network_stream_frame_details);
	  xdr_destroy (&xdr_network_stream_pixel_data);
	  
	  pthread_mutex_unlock ( &network_buffer_copy_lock );
	  
	  frame_number++;
	  
	} // while (is_broken_pipe == 0)
      
      close(new_fd);
      
    } // while(1)
}


void ColourPalette (float value, float col[])
{
  col[0] = value;
  col[1] = 0.;
  col[2] = 1.F - value;
}


int IsBenckSectionFinished (double minutes, double elapsed_time)
{
  int is_bench_section_finished = 0;
  
  
  if (elapsed_time > minutes * 60.)
    {
      is_bench_section_finished = 1;
    }
#ifndef NOMPI
  MPI_Bcast (&is_bench_section_finished, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
  
  if (is_bench_section_finished)
    {
      return 1;
    }
  return 0;
}


void usage (char *progname)
{
  fprintf (timings_ptr, "Usage: %s path of the input files and minutes for benchmarking\n", progname);
  fprintf (timings_ptr, "if one wants to do a benchmark\n");
  fprintf (timings_ptr, "the following files must be present in the path specified:\n");
  fprintf (timings_ptr, "config.dat, pars.asc rt_pars.asc\n");
}


int main (int argc, char *argv[])
{
  // main function needed to perform the entire simulation. Some
  // simulation paramenters and performance statistics are outputted on
  // standard output
  
  double simulation_time;
  double minutes;
  double fluid_solver_time;
  double fluid_solver_and_vr_time;
  double fluid_solver_and_is_time;
  double vr_without_compositing_time;
  
  int cycle_id;
  int total_time_steps, time_step, stability;
  int depths;
  
  int fluid_solver_time_steps;
  int fluid_solver_and_vr_time_steps;
  int fluid_solver_and_is_time_steps;
  int vr_without_compositing_time_steps;
  
  pthread_t network_thread;
  pthread_attr_t pthread_attrib;
  
#ifdef STEER
  int    reg_num_cmds;
  int    reg_cmds[REG_INITIAL_NUM_CMDS];
  char** steer_changed_param_labels;
  char** steer_recvd_cmd_params;
  
  SteerParams steer;
  
  
  steer_changed_param_labels = Alloc_string_array (REG_MAX_STRING_LENGTH,
						   REG_MAX_NUM_STR_PARAMS);
  steer_recvd_cmd_params = Alloc_string_array (REG_MAX_STRING_LENGTH,
					       REG_MAX_NUM_STR_CMDS);
  
  int reg_finished;
#endif // STEER
  LBM lbm;
  
  Net net;
  
  
#ifndef NOMPI
  net.err = MPI_Init (&argc, &argv);
  net.err = MPI_Comm_size (MPI_COMM_WORLD, &net.procs);
  net.err = MPI_Comm_rank (MPI_COMM_WORLD, &net.id);
#else
  net.procs = 1;
  net.id = 0;
#endif
  
  double total_time = myClock ();
  
  char *input_file_path( argv[1] );
  
  char input_config_name[256];
  char input_parameters_name[256];
  char output_config_name[256];
  char vis_parameters_name[256];
  char output_image_name[256];
  char timings_name[256];
  char procs_string[256];
  char image_name[256];
  
  
  strcpy ( input_config_name , input_file_path );
  strcat ( input_config_name , "/config.dat" );

  strcpy ( input_parameters_name , input_file_path );
  strcat ( input_parameters_name , "/pars.asc" );
  
  strcpy ( output_config_name , input_file_path );
  strcat ( output_config_name , "/out.dat" );
  
  strcpy ( vis_parameters_name , input_file_path );
  strcat ( vis_parameters_name , "/rt_pars.asc" );
  
  strcpy ( output_image_name , input_file_path );
  
  sprintf ( procs_string, "%i", net.procs);
  strcpy ( timings_name , input_file_path );
  strcat ( timings_name , "/timings" );
  strcat ( timings_name , procs_string );
  strcat ( timings_name , ".asc" );

  if (net.id == 0)
    {
      timings_ptr = fopen (timings_name, "w");
    }
  
  if (argc != 2 && argc != 3)
    {
      if (net.id == 0) usage(argv[0]);
      
#ifndef NOMPI
      net.err = MPI_Abort (MPI_COMM_WORLD, 1);
      net.err = MPI_Finalize ();
#else
      exit(1);
#endif
    }
  
  if (argc == 3)
    {
      is_bench = 1;
      minutes = atof( argv[2] );
    }
  else
    {
      is_bench = 0;
    }
  
  if (net.id == 0)
    {
      fprintf (timings_ptr, "***********************************************************\n");
      fprintf (timings_ptr, "Opening parameters file:\n %s\n", input_parameters_name);
      fprintf (timings_ptr, "Opening config file:\n %s\n", input_config_name);
      fprintf (timings_ptr, "Opening vis parameters file:\n %s\n\n", vis_parameters_name);
    }
  
#ifdef STEER
  lbmReadParameters (input_parameters_name, &lbm, &net, &steer);
#else
  lbmReadParameters (input_parameters_name, &lbm, &net);
#endif
  
  
  if(net.id == 0)
    {
      xdrSendBuffer_pixel_data = (char *)malloc(pixel_data_bytes);
      xdrSendBuffer_frame_details = (char *)malloc(frame_details_bytes);
      
      pthread_mutex_init (&network_buffer_copy_lock, NULL);
      pthread_cond_init (&network_send_frame, NULL);
      
      pthread_attr_init (&pthread_attrib);
      pthread_attr_setdetachstate (&pthread_attrib, PTHREAD_CREATE_JOINABLE);
      
      pthread_create (&network_thread, &pthread_attrib, hemeLB_network, NULL);
    }
  
  
#ifdef STEER
  // create the derived datatype for the MPI_Bcast
  int steer_count = 24;
  int steer_blocklengths[24] = {1, 1, REG_MAX_NUM_STR_CMDS, 1,
				1, 1, 1, 1, 1,
				1, 1, 1,
				1, 1, 1,
				1, 1, 1,
				1, 1, 1, 1, 1, 1, 1,
				1};
#ifndef NOMPI
  MPI_Datatype steer_types[24] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT,
				  MPI_DOUBLE, MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT,
				  MPI_FLOAT, MPI_FLOAT, MPI_FLOAT,
				  MPI_FLOAT, MPI_FLOAT, MPI_FLOAT,
				  MPI_INT, MPI_INT, MPI_INT,
				  MPI_FLOAT, MPI_FLOAT, MPI_FLOAT, MPI_FLOAT, MPI_FLOAT,
				  MPI_UB};
  
  MPI_Aint steer_disps[24];
  MPI_Datatype MPI_steer_type;
  
  // calculate displacements
  
  steer_disps[0] = 0;
  
  for(int i = 1; i < steer_count; i++) {
    switch(steer_types[i - 1]) {
    case MPI_INT:
      steer_disps[i] = steer_disps[i - 1] + (sizeof(int) * steer_blocklengths[i - 1]);
      break;
    case MPI_DOUBLE:
      steer_disps[i] = steer_disps[i - 1] + (sizeof(double) * steer_blocklengths[i - 1]);
      break;
    case MPI_FLOAT:
      steer_disps[i] = steer_disps[i - 1] + (sizeof(float) * steer_blocklengths[i - 1]);
      break;
    }
  }
  
  MPI_Type_struct (steer_count, steer_blocklengths, steer_disps, steer_types, &MPI_steer_type);
  MPI_Type_commit (&MPI_steer_type);
#endif
  
  
  // initialize the steering library
  if(net.id == 0)
    {
      Steering_enable (REG_TRUE);
      
      reg_num_cmds = 2;
      reg_cmds[0] = REG_STR_STOP;
      reg_cmds[1] = REG_STR_PAUSE_INTERNAL;
      steer.status = Steering_initialize ("HemeLB", reg_num_cmds, reg_cmds);
    }
  
  // broadcast/collect status
#ifndef NOMPI
  net.err = MPI_Bcast (&steer.status, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
  
  // if broken, quit
  if(steer.status == REG_FAILURE)
    {
#ifndef NOMPI
      net.err = MPI_Finalize ();
#endif
      return REG_FAILURE;
    }
#endif // STEER
  
  lbmInit (input_config_name, &lbm, &net);
  
  if (netFindTopology (&net, &depths) == 0)
    {
      fprintf (timings_ptr, "MPI_Attr_get failed, aborting\n");
#ifndef NOMPI
      MPI_Abort(MPI_COMM_WORLD, 1);
#endif
    }
  
  netInit (&lbm, &net);
  
  lbmSetInitialConditions (&net);
  
  visInit (&net, &vis);
  
  stability = STABLE;
  
#ifdef STEER

  // register params with RealityGrid here
  if(net.id == 0)
    {
      // read only and only if displaying
      
//      steer.status = Register_param("Display Host", REG_FALSE,
//				    (void*)(&host_name), REG_CHAR, "", "");
      
      // LBM params
      steer.status = Register_param("Tau", REG_TRUE,
				    (void*)(&steer.tau), REG_DBL, "0.5", "");
      steer.status = Register_param("Tolerance", REG_TRUE,
				    (void*)(&steer.tolerance), REG_DBL, "0.0", "0.1");
      steer.status = Register_param("Max time steps",REG_TRUE,
				    (void*)(&steer.max_cycles), REG_INT, "1", "");
      steer.status = Register_param("Conv frequency", REG_TRUE,
				    (void*)(&steer.conv_freq), REG_INT, "1", "");
      steer.status = Register_param("Checkpoint frequency", REG_TRUE,
				    (void*)(&steer.period), REG_INT, "1", "");
      // Vis params
      steer.status = Register_param("Longitude", REG_TRUE,
				    (void *)(&steer.longitude), REG_FLOAT, "", "");
      steer.status = Register_param("Latitude", REG_TRUE,
				    (void *)(&steer.latitude), REG_FLOAT, "", "");
      steer.status = Register_param("Zoom", REG_TRUE,
				    (void *)(&steer.zoom), REG_FLOAT, "0.0", "");
      steer.status = Register_param("Image output frequency", REG_TRUE,
				    (void*)(&steer.image_freq), REG_INT, "1", "");
      steer.status = Register_param("Flow field type", REG_TRUE,
				    (void*)(&steer.flow_field_type), REG_INT, "0", "2");
      steer.status = Register_param("Vis mode", REG_TRUE,
				    (void*)(&steer.mode), REG_INT, "0", "2");
      steer.status = Register_param("Absorption factor", REG_TRUE,
				    (void *)(&steer.abs_factor), REG_FLOAT, "0.0", "");
      steer.status = Register_param("Cutoff", REG_TRUE,
				    (void *)(&steer.cutoff), REG_FLOAT, "0.0", "1.0");
      steer.status = Register_param("Max density", REG_TRUE,
				    (void *)(&steer.max_density), REG_FLOAT, "0.0", "");
      steer.status = Register_param("Max velocity", REG_TRUE,
				    (void *)(&steer.max_velocity), REG_FLOAT, "0.0", "");
      steer.status = Register_param("Max stress", REG_TRUE,
				    (void *)(&steer.max_stress), REG_FLOAT, "0.0", "");
    }
  
#ifndef NOMPI
  // broadcast/collect status
  net.err = MPI_Bcast(&steer.status, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
  
  // if broken, quit
  if(steer.status == REG_FAILURE)
    {
#ifndef NOMPI
      net.err = MPI_Finalize ();
#endif
      return REG_FAILURE;
    }

  reg_finished = 0;

  if(net.id == 0)
    {
      fprintf (timings_ptr, "STEER: RealityGrid library initialized and parameters registered.\n");
      fflush (timings_ptr);
    }
#endif // STEER
  
#ifdef STEER
  visReadParameters (vis_parameters_name, &net, &vis, &steer);
#else
  visReadParameters (vis_parameters_name, &net, &vis);
#endif
  
#ifdef STEER
  int steering_time_step = 0;
#endif
  
  if (!is_bench)
    {
      int is_finished = 0;
      
      total_time_steps = 0;
      
      simulation_time = myClock ();
      
      for (cycle_id = 0; cycle_id < lbm.cycles_max && !is_finished; cycle_id++)
	{
	  for (time_step = 0; time_step < lbm.period; time_step++)
	    {
	      ++total_time_steps;
	      
	      // globalLongitude += 1.F;
	      // visUpdateLongitude (vis_parameters_name, &net, &vis);
	      
	      int perform_rt = 0;
	      
#ifdef STEER
	      // call steering control
	      if (net.id == 0)
		{
		  steer.status = Steering_control (++steering_time_step,
						   &steer.num_params_changed,
						   steer_changed_param_labels,
						   &steer.num_recvd_cmds,
						   steer.recvd_cmds,
						   steer_recvd_cmd_params);
		}
	      
#ifndef NOMPI
	      // broadcast/collect everything
	      net.err = MPI_Bcast (&steer, 1, MPI_steer_type, 0, MPI_COMM_WORLD);
#endif
	      if (steer.status != REG_SUCCESS)
		{
		  fprintf (stderr, "STEER: I am %d and I detected that Steering_control failed.\n", net.id);
		  fflush(stderr);  
		  continue;
		}
	      
	      // process commands received
	      for (int i = 0; i < steer.num_recvd_cmds; i++)
		{
		  switch (steer.recvd_cmds[i])
		    {
		    case REG_STR_STOP:
		      fprintf (stderr, "STEER: I am %d and I've been told to STOP.\n", net.id);
		      fflush(stderr);
		      reg_finished = 1;
		      break;
		    }
		} // end of command processing
	      
	      // process changed params
	      // not bothered what changed, just copy across...
	      
	      if (steer.num_params_changed > 0)
		{
		  fprintf (stderr, "STEER: I am %d and I was told that %d params changed.\n", net.id, steer.num_params_changed);
		  fflush(stderr);
		  
		  lbmUpdateParameters (&lbm, &steer);
		  
		  visUpdateParameters (&vis, &steer);
		}
	      // end of param processing
	      
#endif // STEER
	      
	      int write_image = 0;
	      int stream_image = 0;
	      
	      if ((time_step + 1)%vis_image_freq == 0)
		{
		  write_image = 1;
		}
	      	      
	      int is_thread_locked = 0;
	      
	      if (total_time_steps%1 == 0)
		{
		  if (net.id == 0)
		    {
		      //pthread_mutex_lock( &network_buffer_copy_lock );
		      is_thread_locked = pthread_mutex_trylock ( &network_buffer_copy_lock );
		    }
#ifndef NOMPI
		  MPI_Bcast (&vis_flow_field_type, 1, MPI_INT, 0, MPI_COMM_WORLD);
		  MPI_Bcast (&is_thread_locked, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
		  if (!is_thread_locked)
		    {
		      stream_image = 1;
		    }
		  
		}
	      if (stream_image || write_image)
		{
		  perform_rt = 1;
		}
	      
	      // Between the visRenderA/B calls, do not change any vis
	      // parameters.
	      
	      if (perform_rt)
		{
		  visRenderA (ColourPalette, &net);
		}
	      lbmVaryBoundaryDensities (cycle_id, time_step, &lbm);
	      
	      stability = lbmCycle (cycle_id, time_step, perform_rt, &lbm, &net);
	      
	      if (write_image)
		{
		  char time_step_string[256];
		  
		  strcpy ( image_name , output_image_name );
		  strcat ( image_name , "/Images/" );
		  
		  int time_steps = time_step + 1;
		  
		  while (time_steps < 100000000)
		    {
		      strcat ( image_name , "0" );
		      time_steps *= 10;
		    }
		  sprintf ( time_step_string, "%i", time_step + 1);
		  strcat ( image_name , time_step_string );
		  strcat ( image_name , ".dat" );
		}
	      if (perform_rt)
		{
		  visRenderB (stream_image, write_image, image_name, ColourPalette, &net);
		}
	      if (net.id == 0)
		{
		  pthread_mutex_unlock (&network_buffer_copy_lock);
		  pthread_cond_signal (&network_send_frame);
		}
	      
	      if (stability == UNSTABLE)
		{
		  printf (" ATTENTION: INSTABILITY CONDITION OCCURRED\n");
		  printf (" AFTER %i total time steps\n", total_time_steps);
		  printf ("EXECUTION IS ABORTED\n");
#ifndef NOMPI
		  MPI_Abort (MPI_COMM_WORLD, 1);
#else
		  exit(1);
#endif
		  is_finished = 1;
		  break;
		}
#ifdef STEER
	      if (reg_finished == 1)
		{
		  is_finished = 1;
		  break;
		}
#endif // STEER
	    }
	  if (net.id == 0)
	    {
	      fprintf (timings_ptr, "cycle id: %i\n", cycle_id+1);
	      printf ("cycle id: %i\n", cycle_id+1);
	    }
	}
      simulation_time = myClock () - simulation_time;
      time_step = (1+min(time_step, lbm.period-1)) * min(cycle_id, lbm.cycles_max-1);
    }
  else // is_bench
    {
      double elapsed_time;
  
      int bench_period = (int)fmax(1., (1e+6 * net.procs) / lbm.total_fluid_sites);
      
      
      // benchmarking HemeLB's fluid solver only
      
      fluid_solver_time = myClock ();
      
      for (time_step = 1; time_step <= 1000000000; time_step++)
	{
	  stability = lbmCycle (0, 0, 0, &lbm, &net);
	  
	  // partial timings
	  elapsed_time = myClock () - fluid_solver_time;
	  
	  if (time_step%bench_period == 1 && net.id == 0)
	    {
	      fprintf (stderr, " FS, time: %.3f, time step: %i, time steps/s: %.3f\n",
		       elapsed_time, time_step, time_step / elapsed_time);
	    }
	  if (time_step%bench_period == 1 &&
	      IsBenckSectionFinished (0.5, elapsed_time))
	    {
	      break;
	    }
	}
      fluid_solver_time_steps = (int)(time_step * minutes / (4. * 0.5) - time_step);
      fluid_solver_time = myClock ();
      
      for (time_step = 1; time_step <= fluid_solver_time_steps; time_step++)
	{
	  stability = lbmCycle (0, 0, 1, &lbm, &net);
	}
      fluid_solver_time = myClock () - fluid_solver_time;
      
      
      // benchmarking HemeLB's fluid solver and volume rendering
      
      vis_flow_field_type = VELOCITY;
      vis_image_freq = 1;
      vis_mode = 0;
      vis_cutoff = -EPSILON;
      vis_compositing = 1;
      fluid_solver_and_vr_time = myClock ();
      
      for (time_step = 1; time_step <= 1000000000; time_step++)
	{
	  visRenderA (ColourPalette, &net);
	  
	  stability = lbmCycle (0, 0, 1, &lbm, &net);
	  
	  visRenderB (0, 0, image_name, ColourPalette, &net);
	  
	  // partial timings
	  elapsed_time = myClock () - fluid_solver_and_vr_time;
	  
	  if (time_step%bench_period == 1 && net.id == 0)
	    {
	      fprintf (stderr, " FS + VR, time: %.3f, time step: %i, time steps/s: %.3f\n",
		       elapsed_time, time_step, time_step / elapsed_time);
	    }
	  if (time_step%bench_period == 1 &&
	      IsBenckSectionFinished (0.5, elapsed_time))
	    {
	      break;
	    }
	}
      fluid_solver_and_vr_time_steps = (int)(time_step * minutes / (4. * 0.5) - time_step);
      fluid_solver_and_vr_time = myClock ();
      
      for (time_step = 1; time_step <= fluid_solver_and_vr_time_steps; time_step++)
	{
	  visRenderA (ColourPalette, &net);
	  
	  stability = lbmCycle (0, 0, 1, &lbm, &net);
	  
	  visRenderB (0, 0, image_name, ColourPalette, &net);
	}
      fluid_solver_and_vr_time = myClock () - fluid_solver_and_vr_time;
      
      
      // benchmarking HemeLB's fluid solver and iso-surface
      
      vis_mode = 1;
      fluid_solver_and_is_time = myClock ();
      
      for (time_step = 1; time_step <= 1000000000; time_step++)
	{
	  visRenderA (ColourPalette, &net);
	  
	  stability = lbmCycle (0, 0, 1, &lbm, &net);
	  
	  visRenderB (0, 0, image_name, ColourPalette, &net);
	  
	  // partial timings
	  elapsed_time = myClock () - fluid_solver_and_is_time;
	  
	  if (time_step%bench_period == 1 && net.id == 0)
	    {
	      fprintf (stderr, " FS + IS, time: %.3f, time step: %i, time steps/s: %.3f\n",
		       elapsed_time, time_step, time_step / elapsed_time);
	    }
	  if (time_step%bench_period == 1 &&
	      IsBenckSectionFinished (0.5, elapsed_time))
	    {
	      break;
	    }
	}
      fluid_solver_and_is_time_steps = (int)(time_step * minutes / (4. * 0.5) - time_step);
      fluid_solver_and_is_time = myClock ();
      
      for (time_step = 1; time_step <= fluid_solver_and_is_time_steps; time_step++)
	{
	  visRenderA (ColourPalette, &net);
	  
	  stability = lbmCycle (0, 0, 1, &lbm, &net);
	  
	  visRenderB (0, 0, image_name, ColourPalette, &net);
	}
      fluid_solver_and_is_time = myClock () - fluid_solver_and_is_time;
      
      
      // benchmarking HemeLB's volume rendering without compositing
      
      vis_mode = 0;
      vis_compositing = 0;
      vr_without_compositing_time = myClock ();
      
      for (time_step = 1; time_step <= 1000000000; time_step++)
	{
	  visRenderA (ColourPalette, &net);
	  
	  // partial timings
	  elapsed_time = myClock () - vr_without_compositing_time;
	  
	  if (time_step%bench_period == 1 && net.id == 0)
	    {
	      fprintf (stderr, " VR - COMP, time: %.3f, time step: %i, time steps/s: %.3f\n",
		       elapsed_time, time_step, time_step / elapsed_time);
	    }
	  if (time_step%bench_period == 1 &&
	      IsBenckSectionFinished (0.5, elapsed_time))
	    {
	      break;
	    }
	}
      vr_without_compositing_time_steps = (int)(time_step * minutes / (4. * 0.5) - time_step);
      vr_without_compositing_time = myClock ();
      
      for (time_step = 1; time_step <= vr_without_compositing_time_steps; time_step++)
	{
	  visRenderA (ColourPalette, &net);
	}
      vr_without_compositing_time = myClock () - vr_without_compositing_time;
    } // is_bench
  

  if (!is_bench)
    {  
      if (net.id == 0)
	{
	  fprintf (timings_ptr, "\n");
	  fprintf (timings_ptr, "threads: %i, machines checked: %i\n\n", net.procs, net_machines);
	  fprintf (timings_ptr, "topology depths checked: %i\n\n", depths);
	  fprintf (timings_ptr, "fluid sites: %i\n\n", lbm.total_fluid_sites);
	  fprintf (timings_ptr, "cycles and total time steps: %i, %i \n\n", cycle_id, total_time_steps);
	  fprintf (timings_ptr, "time steps per second: %.3f\n\n", total_time_steps / simulation_time);
	}
    }
  else  // is_bench
    {
      if (net.id == 0)
	{
	  fprintf (timings_ptr, "\n---------- BENCHMARK RESULTS ----------\n");
	  
	  fprintf (timings_ptr, "threads: %i, machines checked: %i\n\n", net.procs, net_machines);
	  fprintf (timings_ptr, "topology depths checked: %i\n\n", depths);
	  fprintf (timings_ptr, "fluid sites: %i\n\n", lbm.total_fluid_sites);
	  fprintf (timings_ptr, " FS, time steps per second: %.3f, MSUPS: %.3f, time: %.3f\n\n",
		   fluid_solver_time_steps / fluid_solver_time,
		   1.e-6 * lbm.total_fluid_sites / (fluid_solver_time / fluid_solver_time_steps),
		   fluid_solver_time);
	  
	  fprintf (timings_ptr, " FS + VR, time steps per second: %.3f, time: %.3f\n\n",
		   fluid_solver_and_vr_time_steps / fluid_solver_and_vr_time, fluid_solver_and_vr_time);
	  
	  fprintf (timings_ptr, " FS + IS, time steps per second: %.3f, time: %.3f\n\n",
		   fluid_solver_and_is_time_steps / fluid_solver_and_is_time, fluid_solver_and_is_time);
	  
	  fprintf (timings_ptr, " VR - COMP, time steps per second: %.3f, time: %.3f\n\n",
		   vr_without_compositing_time_steps / vr_without_compositing_time, vr_without_compositing_time);
	}
    }
  
  if (net.id == 0)
    {
      fprintf (timings_ptr, "Opening output config file:\n %s\n\n", output_config_name);
      fflush (timings_ptr);
    }
  net.fo_time = myClock ();
  
  //lbmWriteConfig (stability, output_config_name, &lbm, &net);
  
  net.fo_time = myClock () - net.fo_time;
  
  if (net.id == 0)
    {
      if (!is_bench)
	{
	  fprintf (timings_ptr, "density  min, max: %le, %le\n", lbm_density_min, lbm_density_max);
	  fprintf (timings_ptr, "velocity min, max: %le, %le\n", lbm_velocity_min, lbm_velocity_max);
	  fprintf (timings_ptr, "stress   min, max: %le, %le\n", lbm_stress_min, lbm_stress_max);
	}
      fprintf (timings_ptr, "\n");
      fprintf (timings_ptr, "domain decomposition time (s):             %.3f\n", net.dd_time);
      fprintf (timings_ptr, "pre-processing buffer management time (s): %.3f\n", net.bm_time);
      fprintf (timings_ptr, "input configuration reading time (s):      %.3f\n", net.fr_time);
      fprintf (timings_ptr, "flow field outputting time (s):            %.3f\n", net.fo_time);
      
      total_time = myClock () - total_time;
      fprintf (timings_ptr, "total time (s):                            %.3f\n\n", total_time);
      
      fprintf (timings_ptr, "Sub-domains info:\n\n");
      
      for (int n = 0; n < net.procs; n++)
	{
	  fprintf (timings_ptr, "rank: %i, fluid sites: %i\n", n, net.fluid_sites[ n ]);
	}
      
      fclose (timings_ptr);
    }
  
  visEnd ();
  netEnd (&net);
  lbmEnd (&lbm);
  
  if (net.id == 0)
    {
      // there are some problems if the following function is called
      
      //pthread_join (network_thread, NULL);
      free(xdrSendBuffer_frame_details);
      free(xdrSendBuffer_pixel_data);
    }
  
  
#ifdef STEER
  if (net.id == 0)
    {
      Steering_finalize ();
      
      fprintf (stderr, "STEER: Steering_finalize () called.\n");
      fflush(stderr);
    }
#endif // STEER
  
#ifndef NOMPI
  net.err = MPI_Finalize ();
#endif
  
  return(0);
}
