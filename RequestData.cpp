#include <winsock2.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include "SimConnect.h"
#include <strsafe.h>
#include <sstream>
#include <mutex>
#include <iostream>
#include <fstream>
#include <string>
#include <limits>

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

typedef websocketpp::client<websocketpp::config::asio_client> client;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// pull out the type of messages sent by our config
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

typedef struct {
    client                      mClient;
    client::connection_ptr      mConnection;
    websocketpp::connection_hdl mConnectionHandle;
    float altitudeValue = 0.0;
    float latitudeValue = 0.0;
    float longitudeValue = 0.0;
    float headingValue = 0.0;
    float speedValue = 0.0;
} ConnectionContext;

static const double PI = 3.14159265358979323846;

// Handlers
void on_open(ConnectionContext* context, websocketpp::connection_hdl handle) {
    std::string msg = "Hello";
    context->mConnectionHandle = handle;
    // context->mClient.send(handle,msg,websocketpp::frame::opcode::text);
    // context->mClient.get_alog().write(websocketpp::log::alevel::app, "Sent Message: "+msg);
}

void on_fail(ConnectionContext* context, websocketpp::connection_hdl hdl) {
    context->mClient.get_alog().write(websocketpp::log::alevel::app, "Connection Failed");
}

// void on_message(client* c, websocketpp::connection_hdl hdl, message_ptr msg) {
//     c->get_alog().write(websocketpp::log::alevel::app, "Received Reply: "+msg->get_payload());
//     c->close(hdl,websocketpp::close::status::normal,"");
// }

// void on_close(client* c, websocketpp::connection_hdl hdl) {
//     c->get_alog().write(websocketpp::log::alevel::app, "Connection Closed");
// }

int     quit = 0;
HANDLE  hSimConnect = NULL;

// A basic structure for a single item of returned data
struct StructOneDatum {
    int        id;
    float    value;
};

// maxReturnedItems is 5 in this case, as the sample requests alt, lat, lon, head and speed
#define maxReturnedItems    5

// A structure that can be used to receive Tagged data
struct StructDatum {
    StructOneDatum  datum[maxReturnedItems];
};

enum EVENT_PDR {
    EVENT_SIM_START,
};

enum DATA_DEFINE_ID {
    DEFINITION_PDR,
};

enum DATA_REQUEST_ID {
    REQUEST_PDR,
};

enum DATA_NAMES {
    DATA_PLANE_ALTITUDE,
    DATA_PLANE_LATITUDE,
    DATA_PLANE_LONGITUDE,
    DATA_PLANE_HEADING_DEGREES_TRUE,
    DATA_GROUND_SPEED,
};

void CALLBACK MyDispatchProcPDR(SIMCONNECT_RECV* pData, DWORD cbData, void *pContext)
{
    HRESULT hr;
    ConnectionContext* connectionContext = (ConnectionContext*)pContext;
    
    switch(pData->dwID)
    {
        case SIMCONNECT_RECV_ID_EVENT:
        {
            SIMCONNECT_RECV_EVENT *evt = (SIMCONNECT_RECV_EVENT*)pData;
            switch(evt->uEventID)
            {
                case EVENT_SIM_START:
                    
                    // Make the call for data every second, but only when it changes and
                    // only that data that has changed
                    hr = SimConnect_RequestDataOnSimObject(hSimConnect, REQUEST_PDR, DEFINITION_PDR,
                        SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_SECOND,
                        SIMCONNECT_DATA_REQUEST_FLAG_CHANGED | SIMCONNECT_DATA_REQUEST_FLAG_TAGGED    );

                    break;

                default:
                   break;
            }
            break;
        }

        case SIMCONNECT_RECV_ID_SIMOBJECT_DATA:
        {
            SIMCONNECT_RECV_SIMOBJECT_DATA *pObjData = (SIMCONNECT_RECV_SIMOBJECT_DATA*)pData;
            
            switch(pObjData->dwRequestID)
            {
                case REQUEST_PDR:
                {
                    int    count    = 0;
                    StructDatum *pS = (StructDatum*)&pObjData->dwData;
            
                    // There can be a minimum of 1 and a maximum of maxReturnedItems
                    // in the StructDatum structure. The actual number returned will
                    // be held in the dwDefineCount parameter.

                    while (count < (int) pObjData->dwDefineCount)
                    {
                        std::ostringstream ss;

                        switch (pS->datum[count].id)
                        {
                        case DATA_PLANE_ALTITUDE:
                            connectionContext->altitudeValue = pS->datum[count].value * 0.3048;
                            printf("DATA_PLANE_ALTITUDE: %lf (raw %f)\n", pS->datum[count].value * 0.3048, pS->datum[count].value);
                            break;

                        case DATA_PLANE_LATITUDE:
                            connectionContext->latitudeValue = pS->datum[count].value * 180 / PI;
                            printf("DATA_PLANE_LATITUDE: %lf (raw %f)\n", pS->datum[count].value * 180 / PI, pS->datum[count].value);
                            break;

                        case DATA_PLANE_LONGITUDE:
                            connectionContext->longitudeValue = pS->datum[count].value * 180 / PI;
                            printf("DATA_PLANE_LONGITUDE: %lf (raw %f)\n", pS->datum[count].value * 180 / PI, pS->datum[count].value);
                            break;

                        case DATA_PLANE_HEADING_DEGREES_TRUE:
                            connectionContext->headingValue = pS->datum[count].value * 180 / PI;
                            printf("DATA_PLANE_HEADING_DEGREES_TRUE: %lf (raw %f)\n", pS->datum[count].value * 180 / PI, pS->datum[count].value);
                            break;

                        case DATA_GROUND_SPEED:
                            connectionContext->speedValue = pS->datum[count].value * 0.514444;
                            printf("DATA_GROUND_SPEED: %lf (raw %f)\n", pS->datum[count].value * 0.514444, pS->datum[count].value);
                            break;

                        default:
                            printf("Unknown datum ID: %d\n", pS->datum[count].id);
                            break;
                        }
                        ++count;
                    }
                    break;
                }

                default:
                   break;
            }
            break;
        }


        case SIMCONNECT_RECV_ID_QUIT:
        {
            quit = 1;
            break;
        }

        default:
            printf("\nUnknown dwID: %d",pData->dwID);
            break;
    }
}

asio::steady_timer* pTimer;

void sendCurrentState(ConnectionContext* context, const asio::error_code& /*e*/)
{
    std::ostringstream ss;

    ss
        << "{ \"position\":{\"latitude\":" << context->latitudeValue
        << ",\"longitude\":" << context->longitudeValue
        << ",\"altitude\":" << context->altitudeValue
        << "},\"heading\":" << context->headingValue
        << ",\"speed\":" << context->speedValue << "}";

    if (context->latitudeValue != 0 && context->longitudeValue != 0) {
        std::string sentmessage = ss.str();
        std::cout << sentmessage << std::endl;
        context->mClient.send(context->mConnectionHandle, sentmessage, websocketpp::frame::opcode::text);
    }
    pTimer->expires_after(asio::chrono::milliseconds(500)); // Make this 0.5 seconds
    pTimer->async_wait(bind(&sendCurrentState, context, ::_1));
}

void testTaggedDataRequest()
{
    HRESULT hr;
    ConnectionContext connectionContext;

    std::ifstream ifs("fileUri.txt");
  
    std::string content((std::istreambuf_iterator<char>(ifs)),(std::istreambuf_iterator<char>()));

    std::string uri = content;

    // std::string uri = "ws://localhost:10000/";
    // std::string uri = "ws://dev01:8479/";

    // set logging policy if needed
    connectionContext.mClient.clear_access_channels(websocketpp::log::alevel::frame_header);
    connectionContext.mClient.clear_access_channels(websocketpp::log::alevel::frame_payload);
    //c.set_error_channels(websocketpp::log::elevel::none);
    
    // Initialize ASIO
    connectionContext.mClient.init_asio();
    pTimer = new asio::steady_timer(connectionContext.mClient.get_io_service(), asio::chrono::seconds(5));
    pTimer->async_wait(bind(&sendCurrentState, &connectionContext, ::_1));
    

    if (SUCCEEDED(SimConnect_Open(&hSimConnect, "Tagged Data", NULL, 0, 0, 0)))
    {
        // Register our handlers
        connectionContext.mClient.set_open_handler(bind(&on_open,&connectionContext,::_1));
        connectionContext.mClient.set_fail_handler(bind(&on_fail,&connectionContext,::_1));
        // c.set_message_handler(bind(&on_message,&c,::_1,::_2));
        // c.set_close_handler(bind(&on_close,&c,::_1));

        // Create a connection to the given URI and queue it for connection once
        // the event loop starts
        websocketpp::lib::error_code ec;
        connectionContext.mConnection = connectionContext.mClient.get_connection(uri, ec);
        connectionContext.mClient.connect(connectionContext.mConnection);
        
        websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;
        m_thread.reset(new websocketpp::lib::thread(&client::run, &connectionContext.mClient));
        
        printf("\nSmith Myers established connection to SimConnect");   
        
        // Set up the data definition, ensuring that all the elements are in Float32 units, to
        // match the StructDatum structure
        // The number of entries in the DEFINITION_PDR definition should be equal to
        // the maxReturnedItems define

        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE ALTITUDE", "Feet",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_ALTITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE LATITUDE", "Radians",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_LATITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE LONGITUDE", "Radians",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_LONGITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE HEADING DEGREES TRUE", "Radians",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_HEADING_DEGREES_TRUE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "GROUND VELOCITY", "Knots",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_GROUND_SPEED);

        // Request a simulation start event
        hr = SimConnect_SubscribeToSystemEvent(hSimConnect, EVENT_SIM_START, "SimStart");

        while( 0 == quit )
        {
            SimConnect_CallDispatch(hSimConnect, MyDispatchProcPDR, (void*)&connectionContext);
            Sleep(1000);
        } 

        hr = SimConnect_Close(hSimConnect);
    }
}

std::string changeUri(void) {
  // change the uri

  std::string localString = "astrobleme";
  std::cout << "\nEnter new uri: ";
  getline(std::cin, localString);

  std::cin >> localString;
  std::cout << localString;
  std::ofstream out("fileUri.txt");
  out << localString;
  out.close();

  return localString;
  
}

int quitNow(void) {

  std::string choice;
  
  std::cout << "\nAre you sure(Y/N)?";
  std::cin >> choice;

  if(choice == "y" || choice == "Y" || choice == "Yes" || choice == "yes") {
    std::cout << "\n\nEnd of line\n";
	  exit(EXIT_SUCCESS);
  }
  else {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 1;
  }
}

void menu(void) {
  // Menu function for uri setup
  std::string modifedUri;
  
  std::fstream fileStream;

  fileStream.open("fileUri.txt");
    if (fileStream.fail()) {
        std::cout << "DataRequest v1.0\n\nFirst Time Setup\n\nPlease enter uri of Simulator: (example) ws://dev01:8479/\n";
        std::cin >> modifedUri;
        std::cout << modifedUri;
        std::ofstream out("fileUri.txt");
        out << modifedUri;
        out.close();
        // file could not be opened meaning it does not exists = First time setup
        menu();
      }
  
  std::ifstream ifs("fileUri.txt");
  
  std::string content((std::istreambuf_iterator<char>(ifs)),(std::istreambuf_iterator<char>()));

  modifedUri = content;

  int menuOption;

  bool bFail; // declaing and initialise variable to detect a false input
  
  std::cout << "\033c";
  std::cout << "DataRequest v1.0\n\nwebsocket uri program\n\n"; //display a title
  std::cout << "Start Sim by hitting any key or please select a function or enter 1 to quit\n";
  std::cout << "\t0. Use current uri (current uri is: " << modifedUri << ").\n";
  std::cout << "\t1. Modify the current uri\n";
  std::cout << "\t2. Quit\n";
  std::cout << "\tUri is currently: " << modifedUri << "\n";
  std::cout << "\nPlease enter a valid option (0 - 1 or 2 to quit):\n";
  std::cin >> menuOption; //store the choice made by the user in the variable menuOption
  
  bFail = std::cin.fail(); //calling the bool and giving it a false value
    
  switch(menuOption){
    case 0:
        break;
    case 1:
        modifedUri = changeUri();
        menu();
        break;
    case 2:
        quitNow();
          if (quitNow() == 1) {
            menu();
          }
        break;
    default:
        // std::cout << menuOption << " is not a valid option, please input a valid option\n";
        // std::cin.clear();
        // std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        // Menu();
        break;
    }
}

int __cdecl _tmain(int argc, _TCHAR* argv[])
{

    menu();

    testTaggedDataRequest();

    return 0;
}
