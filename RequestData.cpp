#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include "SimConnect.h"
#include <strsafe.h>

int     quit = 0;
HANDLE  hSimConnect = NULL;

// A basic structure for a single item of returned data
struct StructOneDatum {
    int        id;
    float    value;
};

// maxReturnedItems is 2 in this case, as the sample only requests
// vertical speed and pitot heat switch data
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
    DATA_VERTICAL_SPEED,
};

void CALLBACK MyDispatchProcPDR(SIMCONNECT_RECV* pData, DWORD cbData, void *pContext)
{
    HRESULT hr;
    
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
                    int    count    = 0;;
                    StructDatum *pS = (StructDatum*)&pObjData->dwData;
            
                    // There can be a minimum of 1 and a maximum of maxReturnedItems
                    // in the StructDatum structure. The actual number returned will
                    // be held in the dwDefineCount parameter.

                    while (count < (int) pObjData->dwDefineCount)
                    {
                        switch (pS->datum[count].id)
                        {
                        case DATA_PLANE_ALTITUDE:
                            printf("\nDATA_PLANE_ALTITUDE = %f", pS->datum[count].value );
                            break;

                        case DATA_PLANE_LATITUDE:
                            printf("\nDATA_PLANE_LATITUDE = %f", pS->datum[count].value );
                            break;

                        case DATA_PLANE_LONGITUDE:
                            printf("\nDATA_PLANE_LONGITUDE = %f", pS->datum[count].value );
                            break;

                        case DATA_PLANE_HEADING_DEGREES_TRUE:
                            printf("\nDATA_PLANE_HEADING_DEGREES_TRUE = %f", pS->datum[count].value );
                            break;

                        case DATA_VERTICAL_SPEED:
                            printf("\nDATA_VERTICAL_SPEED = %f", pS->datum[count].value );
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

void testTaggedDataRequest()
{
    HRESULT hr;

    if (SUCCEEDED(SimConnect_Open(&hSimConnect, "Tagged Data", NULL, 0, 0, 0)))
    {
        printf("\nSmith Myers established connection to SimConnect");   
        
        // Set up the data definition, ensuring that all the elements are in Float32 units, to
        // match the StructDatum structure
        // The number of entries in the DEFINITION_PDR definition should be equal to
        // the maxReturnedItems define

        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE ALTITUDE", "feet",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_ALTITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE LONGITUDE", "feet",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_LATITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE ALTITUDE", "feet",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_LONGITUDE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "PLANE HEADING DEGREES TRUE", "Radians",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_PLANE_HEADING_DEGREES_TRUE);
        hr = SimConnect_AddToDataDefinition(hSimConnect, DEFINITION_PDR, "VERTICAL SPEED", "feet per second",
                                            SIMCONNECT_DATATYPE_FLOAT32, 0, DATA_VERTICAL_SPEED);

        // Request a simulation start event
        hr = SimConnect_SubscribeToSystemEvent(hSimConnect, EVENT_SIM_START, "SimStart");

        while( 0 == quit )
        {
            SimConnect_CallDispatch(hSimConnect, MyDispatchProcPDR, NULL);
            Sleep(1);
        } 

        hr = SimConnect_Close(hSimConnect);
    }
}

int __cdecl _tmain(int argc, _TCHAR* argv[])
{

    testTaggedDataRequest();

    return 0;
}
