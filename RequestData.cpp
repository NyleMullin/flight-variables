#include <winsock2.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include "SimConnect.h"
#include <strsafe.h>
#include <sstream>
#include <mutex>

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


// Handlers
void on_open(ConnectionContext* context, websocketpp::connection_hdl handle) {
    std::string msg = "Hello";
    context->mConnectionHandle = handle;
    context->mClient.send(handle,msg,websocketpp::frame::opcode::text);
    context->mClient.get_alog().write(websocketpp::log::alevel::app, "Sent Message: "+msg);
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
#define maxReturnedItems    1

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
    DATA_VERTICAL_SPEED,
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
                        // std::string s = ss.str();

                        // ss << "{ position: { latitude: " << latS << ", longitude: " << lonS << ", altitude: " << aS << ",}, heading: " << hS << ", speed: " << sS << ",}";
                        // connectionContext->mClient.send(connectionContext->mConnectionHandle, ss.str(), websocketpp::frame::opcode::text);

                        switch (pS->datum[count].id)
                        {
                        case DATA_PLANE_ALTITUDE:
                            connectionContext->altitudeValue = pS->datum[count].value;
                            // printf("\nDATA_PLANE_ALTITUDE: %d", pS->datum[count].value);
                            break;

                        case DATA_PLANE_LATITUDE:
                            connectionContext->latitudeValue = pS->datum[count].value;
                            // printf("\nDATA_PLANE_LATITUDE: %d", pS->datum[count].value);
                            break;

                        case DATA_PLANE_LONGITUDE:
                            connectionContext->longitudeValue = pS->datum[count].value;
                            // printf("\nDATA_PLANE_LONGITUDE: %d", pS->datum[count].value);
                            break;

                        case DATA_PLANE_HEADING_DEGREES_TRUE:
                            connectionContext->headingValue = pS->datum[count].value;
                            // printf("\nDATA_PLANE_HEADING_DEGREES_TRUE: %d", pS->datum[count].value);
                            break;

                        case DATA_VERTICAL_SPEED:
                            connectionContext->speedValue = pS->datum[count].value;
                            // printf("\nDATA_VERTICAL_SPEED: %d", pS->datum[count].value);
                            break;

                        default:
                            printf("\nUnknown datum ID: %d", pS->datum[count].id);
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

    if (context->latitudeValue =! 0 && context->longitudeValue != 0) {
        context->mClient.send(context->mConnectionHandle, ss.str(), websocketpp::frame::opcode::text);
    }
    pTimer->expires_after(asio::chrono::seconds(1));
    pTimer->async_wait(bind(&sendCurrentState, context, ::_1));
}

void testTaggedDataRequest()
{
    HRESULT hr;
    ConnectionContext connectionContext;

    std::string uri = "ws://localhost:10000/";

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
        // Start the ASIO io_service run loop
        // c.run();
        
        printf("\nSmith Myers established connection to SimConnect");   
        
        // Set up the data definition, ensuring that all the elements are in Float32 units, to
        // match the StructDatum structure
        // The number of entries in the DEFINITION_PDR definition should be equal to
        // the maxReturnedItems define

        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE ALTITUDE", "feet",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_ALTITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE LATITUDE", "feet",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_LATITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE LONGITUDE", "feet",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_LONGITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE HEADING DEGREES TRUE", "Radians",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_HEADING_DEGREES_TRUE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "VERTICAL SPEED", "feet per second",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_VERTICAL_SPEED);

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

int __cdecl _tmain(int argc, _TCHAR* argv[])
{

    testTaggedDataRequest();

    return 0;
}
