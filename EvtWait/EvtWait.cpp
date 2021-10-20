// EvtWait.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <winevt.h>
#include <sddl.h>
#include <strsafe.h>


#pragma comment(lib, "wevtapi.lib")

DWORD WINAPI SubscriptionCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent);
// 以XML字符串显示
DWORD PrintEventAsXML(EVT_HANDLE hEvent);
// 打印事件的System段内容
DWORD PrintEventSystemData(EVT_HANDLE hEvent);
// 打印事件的EventData段内容
DWORD PrintEventUserData(EVT_HANDLE hEvent);
// 只打印感兴趣的值
DWORD PrintInterestedEventValues(EVT_HANDLE hEvent);
void GetCreationTime(LPWSTR creationTime, DWORD creationTimeSize, DWORD serialNumber, PEVT_VARIANT valArray, DWORD sysPropertyCount);


// The callback that receives the events that match the query criteria. 
DWORD WINAPI SubscriptionCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent)
{
	UNREFERENCED_PARAMETER(pContext);

	DWORD status = ERROR_SUCCESS;

	switch (action)
	{
		// You should only get the EvtSubscribeActionError action if your subscription flags 
		// includes EvtSubscribeStrict and the channel contains missing event records.
	case EvtSubscribeActionError:
		if (ERROR_EVT_QUERY_RESULT_STALE == (DWORD)hEvent)
		{
			wprintf(L"The subscription callback was notified that event records are missing.\n");
			// Handle if this is an issue for your application.
		}
		else
		{
			wprintf(L"The subscription callback received the following Win32 error: %lu\n", (DWORD)hEvent);
		}
		break;

	case EvtSubscribeActionDeliver:
		if (ERROR_SUCCESS != (status = PrintEventAsXML(hEvent)))
		{
			goto cleanup;
		}
		if (ERROR_SUCCESS != (status = PrintEventSystemData(hEvent)))
		{
			goto cleanup;
		}
		if (ERROR_SUCCESS != (status = PrintEventUserData(hEvent)))
		{
			goto cleanup;
		}	

		if (ERROR_SUCCESS != (status = PrintInterestedEventValues(hEvent)))
		{
			goto cleanup;
		}
		break;

	default:
		wprintf(L"SubscriptionCallback: Unknown action.\n");
	}

cleanup:

	if (ERROR_SUCCESS != status)
	{
		// End subscription - Use some kind of IPC mechanism to signal
		// your application to close the subscription handle.
	}

	return status; // The service ignores the returned status.
}

// Render the event as an XML string and print it.
DWORD PrintEventAsXML(EVT_HANDLE hEvent)
{
	DWORD status = ERROR_SUCCESS;
	DWORD dwBufferSize = 0;
	DWORD dwBufferUsed = 0;
	DWORD dwPropertyCount = 0;
	LPWSTR pRenderedContent = NULL;


	if (!EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, pRenderedContent, &dwBufferUsed, &dwPropertyCount))
	{
		if (ERROR_INSUFFICIENT_BUFFER == (status = GetLastError()))
		{
			dwBufferSize = dwBufferUsed;
			pRenderedContent = (LPWSTR)malloc(dwBufferSize);
			if (pRenderedContent)
			{
				EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, pRenderedContent, &dwBufferUsed, &dwPropertyCount);
			}
			else
			{
				wprintf(L"malloc failed\n");
				status = ERROR_OUTOFMEMORY;
				goto cleanup;
			}
		}

		if (ERROR_SUCCESS != (status = GetLastError()))
		{
			wprintf(L"EvtRender failed with %d\n", status);
			goto cleanup;
		}
	}

	wprintf(L"%s\n\n", pRenderedContent);
	
cleanup:

	if (pRenderedContent)
		free(pRenderedContent);

	//system("pause");
	return status;		
}

DWORD PrintEventSystemData(EVT_HANDLE hEvent)
{
	DWORD status = ERROR_SUCCESS;
	EVT_HANDLE hContext = NULL;
	DWORD dwBufferSize = 0;
	DWORD dwBufferUsed = 0;
	DWORD dwPropertyCount = 0;
	PEVT_VARIANT pRenderedValues = NULL;
	WCHAR wsGuid[50];
	LPWSTR pwsSid = NULL;
	ULONGLONG ullTimeStamp = 0;
	ULONGLONG ullNanoseconds = 0;
	SYSTEMTIME st;
	FILETIME ft;

	// Identify the components of the event that you want to render. In this case,
	// render the system section of the event.
	hContext = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);
	if (NULL == hContext)
	{
		wprintf(L"EvtCreateRenderContext failed with %lu\n", status = GetLastError());
		goto cleanup;
	}

	// When you render the user data or system section of the event, you must specify
	// the EvtRenderEventValues flag. The function returns an array of variant values 
	// for each element in the user data or system section of the event. For user data
	// or event data, the values are returned in the same order as the elements are 
	// defined in the event. For system data, the values are returned in the order defined
	// in the EVT_SYSTEM_PROPERTY_ID enumeration.
	if (!EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount))
	{
		if (ERROR_INSUFFICIENT_BUFFER == (status = GetLastError()))
		{
			dwBufferSize = dwBufferUsed;
			pRenderedValues = (PEVT_VARIANT)malloc(dwBufferSize);
			if (pRenderedValues)
			{
				EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount);
			}
			else
			{
				wprintf(L"malloc failed\n");
				status = ERROR_OUTOFMEMORY;
				goto cleanup;
			}
		}

		if (ERROR_SUCCESS != (status = GetLastError()))
		{
			wprintf(L"EvtRender failed with %d\n", GetLastError());
			goto cleanup;
		}
	}

	// Print the values from the System section of the element.
	wprintf(L"Provider Name: %s\n", pRenderedValues[EvtSystemProviderName].StringVal);
	if (NULL != pRenderedValues[EvtSystemProviderGuid].GuidVal)
	{
		StringFromGUID2(*(pRenderedValues[EvtSystemProviderGuid].GuidVal), wsGuid, sizeof(wsGuid) / sizeof(WCHAR));
		wprintf(L"Provider Guid: %s\n", wsGuid);
	}
	else
	{
		wprintf(L"Provider Guid: NULL");
	}

	DWORD EventID = pRenderedValues[EvtSystemEventID].UInt16Val;
	if (EvtVarTypeNull != pRenderedValues[EvtSystemQualifiers].Type)
	{
		EventID = MAKELONG(pRenderedValues[EvtSystemEventID].UInt16Val, pRenderedValues[EvtSystemQualifiers].UInt16Val);
	}
	wprintf(L"EventID: %lu\n", EventID);

	wprintf(L"Version: %u\n", (EvtVarTypeNull == pRenderedValues[EvtSystemVersion].Type) ? 0 : pRenderedValues[EvtSystemVersion].ByteVal);
	wprintf(L"Level: %u\n", (EvtVarTypeNull == pRenderedValues[EvtSystemLevel].Type) ? 0 : pRenderedValues[EvtSystemLevel].ByteVal);
	wprintf(L"Task: %hu\n", (EvtVarTypeNull == pRenderedValues[EvtSystemTask].Type) ? 0 : pRenderedValues[EvtSystemTask].UInt16Val);
	wprintf(L"Opcode: %u\n", (EvtVarTypeNull == pRenderedValues[EvtSystemOpcode].Type) ? 0 : pRenderedValues[EvtSystemOpcode].ByteVal);
	wprintf(L"Keywords: 0x%I64x\n", pRenderedValues[EvtSystemKeywords].UInt64Val);

	ullTimeStamp = pRenderedValues[EvtSystemTimeCreated].FileTimeVal;
	ft.dwHighDateTime = (DWORD)((ullTimeStamp >> 32) & 0xFFFFFFFF);
	ft.dwLowDateTime = (DWORD)(ullTimeStamp & 0xFFFFFFFF);

	FileTimeToSystemTime(&ft, &st);
	ullNanoseconds = (ullTimeStamp % 10000000) * 100; // Display nanoseconds instead of milliseconds for higher resolution
	wprintf(L"TimeCreated SystemTime: %02d/%02d/%02d %02d:%02d:%02d.%I64u)\n",
		st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, ullNanoseconds);

	wprintf(L"EventRecordID: %I64u\n", pRenderedValues[EvtSystemEventRecordId].UInt64Val);

	if (EvtVarTypeNull != pRenderedValues[EvtSystemActivityID].Type)
	{
		StringFromGUID2(*(pRenderedValues[EvtSystemActivityID].GuidVal), wsGuid, sizeof(wsGuid) / sizeof(WCHAR));
		wprintf(L"Correlation ActivityID: %s\n", wsGuid);
	}

	if (EvtVarTypeNull != pRenderedValues[EvtSystemRelatedActivityID].Type)
	{
		StringFromGUID2(*(pRenderedValues[EvtSystemRelatedActivityID].GuidVal), wsGuid, sizeof(wsGuid) / sizeof(WCHAR));
		wprintf(L"Correlation RelatedActivityID: %s\n", wsGuid);
	}

	wprintf(L"Execution ProcessID: %lu\n", pRenderedValues[EvtSystemProcessID].UInt32Val);
	wprintf(L"Execution ThreadID: %lu\n", pRenderedValues[EvtSystemThreadID].UInt32Val);
	wprintf(L"Channel: %s\n", (EvtVarTypeNull == pRenderedValues[EvtSystemChannel].Type) ? L"" : pRenderedValues[EvtSystemChannel].StringVal);
	wprintf(L"Computer: %s\n\n", pRenderedValues[EvtSystemComputer].StringVal);

	if (EvtVarTypeNull != pRenderedValues[EvtSystemUserID].Type)
	{
		if (ConvertSidToStringSid(pRenderedValues[EvtSystemUserID].SidVal, &pwsSid))
		{
			wprintf(L"Security UserID: %s\n", pwsSid);
			LocalFree(pwsSid);
		}
	}

cleanup:

	if (hContext)
		EvtClose(hContext);

	if (pRenderedValues)
		free(pRenderedValues);

	return status;
}

// 打印EventData
DWORD PrintEventUserData(EVT_HANDLE hEvent)
{
	DWORD status = ERROR_SUCCESS;
	EVT_HANDLE hContext = NULL;
	DWORD dwBufferSize = 0;
	DWORD dwBufferUsed = 0;
	DWORD dwPropertyCount = 0;
	PEVT_VARIANT pRenderedValues = NULL;
	WCHAR wsGuid[50];
	LPWSTR pwsSid = NULL;
	ULONGLONG ullTimeStamp = 0;
	ULONGLONG ullNanoseconds = 0;
	SYSTEMTIME st;
	FILETIME ft;

	// Identify the components of the event that you want to render. In this case,
	// render the system section of the event.
	hContext = EvtCreateRenderContext(0, NULL, EvtRenderContextUser);
	if (NULL == hContext)
	{
		wprintf(L"EvtCreateRenderContext failed with %lu\n", status = GetLastError());
		goto cleanup;
	}

	// When you render the user data or system section of the event, you must specify
	// the EvtRenderEventValues flag. The function returns an array of variant values 
	// for each element in the user data or system section of the event. For user data
	// or event data, the values are returned in the same order as the elements are 
	// defined in the event. For system data, the values are returned in the order defined
	// in the EVT_SYSTEM_PROPERTY_ID enumeration.
	if (!EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount))
	{
		if (ERROR_INSUFFICIENT_BUFFER == (status = GetLastError()))
		{
			dwBufferSize = dwBufferUsed;
			pRenderedValues = (PEVT_VARIANT)malloc(dwBufferSize);
			if (pRenderedValues)
			{
				EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount);
			}
			else
			{
				wprintf(L"malloc failed\n");
				status = ERROR_OUTOFMEMORY;
				goto cleanup;
			}
		}

		if (ERROR_SUCCESS != (status = GetLastError()))
		{
			wprintf(L"EvtRender failed with %d\n", GetLastError());
			goto cleanup;
		}
	}

	// Print the values from the System section of the element.
	wprintf(L"Process Name: %s\n", pRenderedValues[5].StringVal);
	wprintf(L"Process ID: %d \n", pRenderedValues[4].Int32Val);
	wprintf(L"Parent Process ID: %d \n\n", pRenderedValues[7].Int32Val);
cleanup:

	if (hContext)
		EvtClose(hContext);

	if (pRenderedValues)
		free(pRenderedValues);

	return status;
}


// 只打印感兴趣的值
DWORD PrintInterestedEventValues(EVT_HANDLE hEvent)
{
	DWORD status = ERROR_SUCCESS;
	EVT_HANDLE hContext = NULL;
	DWORD dwBufferSize = 0;
	DWORD dwBufferUsed = 0;
	DWORD dwPropertyCount = 0;
	PEVT_VARIANT pRenderedValues = NULL;
	LPCWSTR ppValues[] = {
		L"Event/System/EventID",
		L"Event/System/TimeCreated/@SystemTime",
		L"Event/System/Computer",
		L"Event/EventData/Data[@Name='NewProcessId']",
		L"Event/EventData/Data[@Name='NewProcessName']",
		L"Event/EventData/Data[@Name='ProcessId']",		
	};
	DWORD count = sizeof(ppValues) / sizeof(LPWSTR);

	// Identify the components of the event that you want to render. In this case,
	// render the provider's name and channel from the system section of the event.
	// To get user data from the event, you can specify an expression such as
	// L"Event/EventData/Data[@Name=\"<data name goes here>\"]". 
	//  list all the components of the event use blow way
	//  //Renders event system properties
	// 	EVT_HANDLE renderContext = EvtCreateRenderContext(NULL, 0, EvtRenderContextSystem);
	// 	//Renders event user properties
	// 	EVT_HANDLE renderUserContext = EvtCreateRenderContext(NULL, 0, EvtRenderContextUser);

	do
	{
		hContext = EvtCreateRenderContext(count, (LPCWSTR*)ppValues, EvtRenderContextValues);
		if (NULL == hContext)
		{
			wprintf(L"EvtCreateRenderContext failed with %lu\n", status = GetLastError());
			break;
		}

		// The function returns an array of variant values for each element or attribute that
		// you want to retrieve from the event. The values are returned in the same order as 
		// you requested them.
		if (!EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount))
		{
			if (ERROR_INSUFFICIENT_BUFFER == (status = GetLastError()))
			{
				dwBufferSize = dwBufferUsed;
				pRenderedValues = (PEVT_VARIANT)malloc(dwBufferSize);
				if (pRenderedValues)
				{
					EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferSize, pRenderedValues, &dwBufferUsed, &dwPropertyCount);
				}
				else
				{
					wprintf(L"malloc failed\n");
					status = ERROR_OUTOFMEMORY;
					break;
				}
			}

			if (ERROR_SUCCESS != (status = GetLastError()))
			{
				wprintf(L"EvtRender failed with %d\n", GetLastError());
				break;
			}
		}

		wchar_t creationTime[500];
		DWORD creationTimeSize = sizeof(creationTime);

		//creation time	
		GetCreationTime(creationTime, creationTimeSize, 1, pRenderedValues, dwPropertyCount);
		wprintf(L"\nEventID: %6hu\n", (pRenderedValues[0].Type == EvtVarTypeNull) ? 0 : pRenderedValues[0].Int16Val);
		wprintf(L"TimeCreated: %s\n", creationTime);
		wprintf(L"Computer: %s\n", (EvtVarTypeNull == pRenderedValues[2].Type) ? L"" : pRenderedValues[2].StringVal);
		wprintf(L"ProcessID: %d\n", (EvtVarTypeNull == pRenderedValues[3].Type) ? 0 : pRenderedValues[3].Int32Val);
		wprintf(L"ProcessName: %s\n", (EvtVarTypeNull == pRenderedValues[4].Type) ? L"" : pRenderedValues[4].StringVal);
		wprintf(L"Parent ProcessID: %d\n\n", (EvtVarTypeNull == pRenderedValues[5].Type) ? 0 : pRenderedValues[5].Int32Val);
	
	} while (false);

	if (hContext)
		EvtClose(hContext);

	if (pRenderedValues)
		free(pRenderedValues);

	return status;
}

void GetCreationTime(LPWSTR creationTime, DWORD creationTimeSize, DWORD serialNumber, PEVT_VARIANT valArray, DWORD sysPropertyCount)
{
	FILETIME FileTime, LocalFileTime;
	__int64 lgTemp;
	lgTemp = valArray[serialNumber].FileTimeVal;
	FileTime.dwLowDateTime = (DWORD)lgTemp;
	FileTime.dwHighDateTime = (DWORD)(lgTemp >> 32);

	SYSTEMTIME SysTime;
	FileTimeToLocalFileTime(&FileTime, &LocalFileTime);
	FileTimeToSystemTime(&LocalFileTime, &SysTime);
	StringCchPrintfW(creationTime, creationTimeSize, L"%02d/%02d/%02d %02d:%02d:%02d.%06d",
		SysTime.wMonth,
		SysTime.wDay,
		SysTime.wYear,
		SysTime.wHour,
		SysTime.wMinute,
		SysTime.wSecond,
		SysTime.wMilliseconds);
}

// "Security" "Microsoft-Windows-Security-Auditing" 4625
int _tmain(int argc, _TCHAR* argv[])
{
	// LPWSTR pwsQuery = L"<QueryList><Query Id=\"0\" Path=\"%S\"><Select Path=\"%S\">*[System[Provider[ @Name='%S'] and (EventID=%S)]]</Select></Query></QueryList>";
	DWORD status = ERROR_SUCCESS;
	EVT_HANDLE hSubscription = NULL;
	LPWSTR pwsPath = L"Security";
	LPWSTR pwsQuery = L"*[System[Provider[@Name='Microsoft-Windows-Security-Auditing'] and (EventID=4688)]]";

	// Subscribe to events beginning with the oldest event in the channel. The subscription
	// will return all current events in the channel and any future events that are raised
	// while the application is active.
	hSubscription = EvtSubscribe(NULL, NULL, pwsPath, pwsQuery, NULL, NULL,
		(EVT_SUBSCRIBE_CALLBACK)SubscriptionCallback, /*EvtSubscribeStartAtOldestRecord*/EvtSubscribeToFutureEvents);
	if (NULL == hSubscription)
	{
		status = GetLastError();

		if (ERROR_EVT_CHANNEL_NOT_FOUND == status)
			wprintf(L"Channel %s was not found.\n", pwsPath);
		else if (ERROR_EVT_INVALID_QUERY == status)
			// You can call EvtGetExtendedStatus to get information as to why the query is not valid.
			wprintf(L"The query \"%s\" is not valid.\n", pwsQuery);
		else
			wprintf(L"EvtSubscribe failed with %lu.\n", status);

		goto cleanup;
	}

	wprintf(L"Hit any key to quit\n\n");
	while (!_kbhit())
		Sleep(10);

cleanup:

	if (hSubscription)
		EvtClose(hSubscription);

	return 0;
}
