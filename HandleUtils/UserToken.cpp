//  Copyright 2015 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//  http ://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "stdafx.h"
#include "UserToken.h"
#include "typed_buffer.h"
#include "ScopedHandle.h"
#include "HandleUtils.h"

#include <vector>

namespace {
	template<typename T> typed_buffer_ptr<T> GetTokenInfo(NativeHandle^ handle,
		TOKEN_INFORMATION_CLASS TokenInformationClass)
	{
		HANDLE h = handle->DangerousGetHandle().ToPointer();
		DWORD dwLength = 0;

		typed_buffer_ptr<T> ret;

		if (!::GetTokenInformation(h, TokenInformationClass, nullptr, 0, &dwLength))
		{
			DWORD dwLastError = ::GetLastError();

			if (dwLastError != ERROR_INSUFFICIENT_BUFFER)
			{
				throw gcnew System::ComponentModel::Win32Exception(dwLastError);
			}
		}

		ret.reset(dwLength);

		if (!::GetTokenInformation(h, TokenInformationClass, ret, dwLength, &dwLength))
		{
			throw gcnew System::ComponentModel::Win32Exception(::GetLastError());
		}

		return ret;
	}

	template<typename T> void GetTokenInfo(NativeHandle^ handle, TOKEN_INFORMATION_CLASS TokenInformationClass, T& value)
	{
		HANDLE h = handle->DangerousGetHandle().ToPointer();
		DWORD dwRetLen = 0;

		if (!::GetTokenInformation(h, TokenInformationClass, &value, sizeof(value), &dwRetLen))
		{
			throw gcnew System::ComponentModel::Win32Exception(::GetLastError());
		}
	}

	DWORD GetTokenInfoDword(NativeHandle^ handle, TOKEN_INFORMATION_CLASS TokenInformationClass)
	{
		HANDLE h = handle->DangerousGetHandle().ToPointer();
		DWORD dwRet = 0;
		DWORD dwRetLen = 0;

		if (!::GetTokenInformation(h, TokenInformationClass, &dwRet, sizeof(dwRet), &dwRetLen))
		{
			throw gcnew System::ComponentModel::Win32Exception(::GetLastError());
		}

		return dwRet;
	}

	unsigned long long LuidToLong(const LUID& luid)
	{
		ULARGE_INTEGER il;

		il.HighPart = luid.HighPart;
		il.LowPart = luid.LowPart;

		return il.QuadPart;
	}	
}

using namespace System;
using namespace System::Security::Principal;
using namespace System::Security::AccessControl;

namespace TokenLibrary {

	array<UserGroup^>^ GetGroupArray(NativeHandle^ token, TOKEN_INFORMATION_CLASS TokenInformationClass)
	{
		typed_buffer_ptr<TOKEN_GROUPS> groups =
			GetTokenInfo<TOKEN_GROUPS>(token, TokenInformationClass);

		System::Collections::Generic::List<UserGroup^>^ ret = gcnew System::Collections::Generic::List<UserGroup^>();

		for (DWORD i = 0; i < groups->GroupCount; ++i)
		{
			SecurityIdentifier^ sid = gcnew SecurityIdentifier(IntPtr(groups->Groups[i].Sid));

			ret->Add(gcnew UserGroup(sid, (GroupFlags)groups->Groups[i].Attributes));
		}

		return ret->ToArray();
	}

	UserToken::UserToken(NativeHandle^ hToken)
	{
		_token = hToken;
	}

	UserToken::~UserToken()
	{
		_token->Close();
	}

	UserGroup^ UserToken::GetUser()
	{
		typed_buffer_ptr<TOKEN_USER> user = GetTokenInfo<TOKEN_USER>(_token, TokenUser);		
		
		return gcnew UserGroup(IntPtr(user->User.Sid), GroupFlags::None);
	}

	TokenType UserToken::GetTokenType()
	{
		typed_buffer_ptr<TOKEN_TYPE> type = GetTokenInfo<TOKEN_TYPE>(_token, ::TokenType);

		if (*type == TokenPrimary)
		{
			return TokenType::Primary;
		}
		else
		{
			return TokenType::Impersonation;
		}
	}

	TokenImpersonationLevel UserToken::GetImpersonationLevel()
	{
		if (GetTokenType() == TokenType::Primary)
		{
			return TokenImpersonationLevel::None;
		}
		else
		{
			typed_buffer_ptr<SECURITY_IMPERSONATION_LEVEL> implevel = GetTokenInfo<SECURITY_IMPERSONATION_LEVEL>(_token, ::TokenImpersonationLevel);

			switch (*implevel)
			{
			case SecurityAnonymous:
				return TokenImpersonationLevel::Anonymous;
			case SecurityIdentification:
				return TokenImpersonationLevel::Identification;
			case SecurityImpersonation:
				return TokenImpersonationLevel::Impersonation;
			case SecurityDelegation:
				return TokenImpersonationLevel::Delegation;
			default:
				return TokenImpersonationLevel::None;
			}
		}
	}

	TokenIntegrityLevel UserToken::GetTokenIntegrityLevel()
	{
		typed_buffer_ptr<TOKEN_MANDATORY_LABEL> tokenil =
			GetTokenInfo<TOKEN_MANDATORY_LABEL>(_token, ::TokenIntegrityLevel);

		return (TokenIntegrityLevel)*GetSidSubAuthority(tokenil->Label.Sid, 0);		
	}

	void UserToken::SetTokenIntegrityLevel(TokenIntegrityLevel token_il)
	{
		typed_buffer_ptr<TOKEN_MANDATORY_LABEL> tokenil =
			GetTokenInfo<TOKEN_MANDATORY_LABEL>(_token, ::TokenIntegrityLevel);

		PDWORD pil = GetSidSubAuthority(tokenil->Label.Sid, 0);

		*pil = (DWORD)token_il;

		if (!::SetTokenInformation(_token->DangerousGetHandle().ToPointer(), 
			::TokenIntegrityLevel, tokenil, (DWORD)tokenil.size()))
		{
			throw gcnew System::ComponentModel::Win32Exception(::GetLastError());
		}
	}

	unsigned long long UserToken::GetAuthenticationId()
	{
		typed_buffer_ptr<TOKEN_STATISTICS> token_stats =
			GetTokenInfo<TOKEN_STATISTICS>(_token, TokenStatistics);

		return LuidToLong(token_stats->AuthenticationId);
	}

	unsigned long long UserToken::GetTokenId()
	{	
		typed_buffer_ptr<TOKEN_STATISTICS> token_stats =
			GetTokenInfo<TOKEN_STATISTICS>(_token, TokenStatistics);

		return LuidToLong(token_stats->TokenId);
	}

	unsigned long long UserToken::GetModifiedId()
	{
		typed_buffer_ptr<TOKEN_STATISTICS> token_stats =
			GetTokenInfo<TOKEN_STATISTICS>(_token, TokenStatistics);

		return LuidToLong(token_stats->ModifiedId);
	}

	int UserToken::GetSessionId()
	{
		return GetTokenInfoDword(_token, ::TokenSessionId);
	}

	System::String^ UserToken::GetSourceName()
	{
		typed_buffer_ptr<TOKEN_SOURCE> token_source =
			GetTokenInfo<TOKEN_SOURCE>(_token, TokenSource);

		int len = 0;
		while (len < 8)
		{
			if (token_source->SourceName[len] == 0)
			{
				break;
			}
			len++;
		}

		return gcnew String(token_source->SourceName, 0, len);
	}

	unsigned long long UserToken::GetSourceId()
	{
		typed_buffer_ptr<TOKEN_SOURCE> token_source =
			GetTokenInfo<TOKEN_SOURCE>(_token, TokenSource);

		return LuidToLong(token_source->SourceIdentifier);
	}

	unsigned long long UserToken::GetTokenOriginId()
	{
		typed_buffer_ptr<TOKEN_ORIGIN> token_origin =
			GetTokenInfo<TOKEN_ORIGIN>(_token, ::TokenOrigin);

		return LuidToLong(token_origin->OriginatingLogonSession);
	}

	UserToken^ UserToken::DuplicateHandle()
	{
		return gcnew UserToken(_token->Duplicate());
	}

	UserToken^ UserToken::DuplicateHandle(unsigned int access_rights)
	{
		return gcnew UserToken(_token->Duplicate(access_rights));
	}

	UserToken^ UserToken::GetLinkedToken()
	{
		TOKEN_LINKED_TOKEN el;

		GetTokenInfo<TOKEN_LINKED_TOKEN>(_token, ::TokenLinkedToken, el);

		if (el.LinkedToken)
		{
			return gcnew UserToken(gcnew NativeHandle(IntPtr(el.LinkedToken)));
		}
		else
		{
			return nullptr;
		}
	}

	TokenElevationType UserToken::GetElevationType()
	{
		typed_buffer_ptr<TOKEN_ELEVATION_TYPE> e_type =
			GetTokenInfo<TOKEN_ELEVATION_TYPE>(_token, ::TokenElevationType);

		switch (*e_type)
		{
		case TokenElevationTypeDefault:
			return TokenElevationType::Default;
		case TokenElevationTypeFull:
			return TokenElevationType::Full;
		case TokenElevationTypeLimited:
			return TokenElevationType::Limited;
		default:
			return TokenElevationType::Default;
		}
	}

	bool UserToken::IsRestricted()
	{
		return !!IsTokenRestricted(_token->DangerousGetHandle().ToPointer());
	}

	array<UserGroup^>^ UserToken::GetGroups()
	{
		return GetGroupArray(_token, ::TokenGroups);
	}

	UserGroup^ UserToken::GetDefaultOwner()
	{
		typed_buffer_ptr<TOKEN_OWNER> owner =
			GetTokenInfo<TOKEN_OWNER>(_token, ::TokenOwner);

		return gcnew UserGroup(IntPtr(owner->Owner), GroupFlags::None);
	}

	UserGroup^ UserToken::GetPrimaryGroup()
	{
		typed_buffer_ptr<TOKEN_PRIMARY_GROUP> group =
			GetTokenInfo<TOKEN_PRIMARY_GROUP>(_token, ::TokenPrimaryGroup);

		return gcnew UserGroup(IntPtr(group->PrimaryGroup), GroupFlags::None);
	}

	RawAcl^ UserToken::GetDefaultDacl()
	{
		typed_buffer_ptr<TOKEN_DEFAULT_DACL> dacl =
			GetTokenInfo<TOKEN_DEFAULT_DACL>(_token, ::TokenDefaultDacl);

		if (dacl->DefaultDacl != nullptr)
		{
			WORD size = dacl->DefaultDacl->AclSize;

			array<byte>^ daclbuf = gcnew array<byte>(size);

			System::Runtime::InteropServices::Marshal::Copy(IntPtr(dacl->DefaultDacl), daclbuf, 0, size);

			return gcnew RawAcl(daclbuf, 0);
		}
		else
		{
			return nullptr;
		}
	}

	array<UserGroup^>^ UserToken::GetRestrictedSids()
	{
		if (IsRestricted())
		{
			return GetGroupArray(_token, ::TokenRestrictedSids);
		}
		else
		{
			return gcnew array<UserGroup^>(0);
		}
	}

	bool UserToken::IsAppContainer()
	{		
		try
		{
			return !!GetTokenInfoDword(_token, ::TokenIsAppContainer);			
		}
		catch (System::ComponentModel::Win32Exception^)
		{
			return false;
		}
	}

	UserGroup^ UserToken::GetPackageSid()
	{
		if (IsAppContainer())
		{
			typed_buffer_ptr<TOKEN_APPCONTAINER_INFORMATION> acinfo =
				GetTokenInfo<TOKEN_APPCONTAINER_INFORMATION>(_token, ::TokenAppContainerSid);

			return gcnew UserGroup(IntPtr(acinfo->TokenAppContainer), GroupFlags::None);
		}
		else
		{
			return nullptr;
		}
	}

	unsigned int UserToken::GetAppContainerNumber()
	{
		if (IsAppContainer())
		{			
			return GetTokenInfoDword(_token, ::TokenAppContainerNumber);
		}
		else
		{
			return 0;
		}
	}

	bool UserToken::IsUIAccess()
	{
		return !!GetTokenInfoDword(_token, ::TokenUIAccess);
	}

	bool UserToken::IsSandboxInert()
	{
		return !!GetTokenInfoDword(_token, ::TokenSandBoxInert);
	}

	bool UserToken::IsVirtualizationAllowed()
	{
		return !!GetTokenInfoDword(_token, ::TokenVirtualizationAllowed);
	}

	bool UserToken::IsVirtualizationEnabled()
	{
		if (IsVirtualizationAllowed())
		{
			return !!GetTokenInfoDword(_token, ::TokenVirtualizationEnabled);
		}
		else
		{
			return false;
		}
	}

	array<UserGroup^>^ UserToken::GetCapabilities()
	{
		if (IsAppContainer())
		{
			return GetGroupArray(_token, ::TokenCapabilities);
		}
		else
		{
			return gcnew array<UserGroup^>(0);
		}
	}

	UserToken^ UserToken::DuplicateToken(TokenType type, TokenImpersonationLevel implevel)
	{
		return DuplicateToken(type, implevel, GetTokenIntegrityLevel());
	}

	UserToken^ UserToken::DuplicateToken(TokenType type, TokenImpersonationLevel implevel, TokenIntegrityLevel token_il)
	{
		if (type == TokenType::Primary)
		{
			return gcnew UserToken(HandleUtils::NativeBridge::CreatePrimaryToken(_token));
		}
		else
		{
			HandleUtils::TokenSecurityLevel level;

			switch (implevel)
			{
			case TokenImpersonationLevel::Anonymous:
				level = HandleUtils::TokenSecurityLevel::Anonymous;
				break;
			case TokenImpersonationLevel::Identification:
				level = HandleUtils::TokenSecurityLevel::Identification;
				break;
			case TokenImpersonationLevel::Impersonation:
				level = HandleUtils::TokenSecurityLevel::Impersonate;
				break;
			case TokenImpersonationLevel::Delegation:
				level = HandleUtils::TokenSecurityLevel::Delegate;
				break;
			default:
				throw gcnew System::ComponentModel::Win32Exception(ERROR_NO_IMPERSONATION_TOKEN);				
			}

			UserToken^ ret = gcnew UserToken(HandleUtils::NativeBridge::CreateImpersonationToken(_token, level));

			if (ret->GetTokenIntegrityLevel() != token_il)
			{
				ret->SetTokenIntegrityLevel(token_il);
			}

			return ret;
		}
	}

	TokenIntegrityLevelPolicy UserToken::GetIntegrityLevelPolicy()
	{
		typed_buffer_ptr<TOKEN_MANDATORY_POLICY> policy = GetTokenInfo<TOKEN_MANDATORY_POLICY>(_token, ::TokenMandatoryPolicy);

		return (TokenIntegrityLevelPolicy)policy->Policy;
	}

	ImpersonateProcess^ UserToken::Impersonate()
	{
		return gcnew ImpersonateProcess(_token->Duplicate(TOKEN_ALL_ACCESS));
	}

	array<TokenPrivilege^>^ UserToken::GetPrivileges()
	{
		typed_buffer_ptr<TOKEN_PRIVILEGES> privs =
			GetTokenInfo<TOKEN_PRIVILEGES>(_token, ::TokenPrivileges);
		array<TokenPrivilege^>^ ret = gcnew array<TokenPrivilege^>(privs->PrivilegeCount);

		for (DWORD i = 0; i < privs->PrivilegeCount; ++i)
		{
			System::String^ name = nullptr;
			System::String^ displayName = "";

			std::vector<wchar_t> buf;
			DWORD dwSize = 0;

			unsigned long long luid = LuidToLong(privs->Privileges[i].Luid);

			::LookupPrivilegeName(nullptr, &privs->Privileges[i].Luid, nullptr, &dwSize);

			buf.resize(dwSize);
			if (LookupPrivilegeName(nullptr, &privs->Privileges[i].Luid, &buf[0], &dwSize))
			{
				name = gcnew String(&buf[0]);
				std::vector<wchar_t> displayBuf;
				DWORD dwDisplaySize = 0;
				DWORD langId = 0;

				::LookupPrivilegeDisplayName(nullptr, &buf[0], nullptr, &dwDisplaySize, &langId);
				displayBuf.resize(dwDisplaySize);

				if (::LookupPrivilegeDisplayName(nullptr, &buf[0], &displayBuf[0], &dwDisplaySize, &langId))
				{
					displayName = gcnew String(&displayBuf[0]);
				}
			}
			else
			{
				name = String::Format("Unknown Privilege 0x{0:X}", luid);
				displayName = "";
			}

			ret[i] = gcnew TokenPrivilege(luid, name, displayName, (TokenPrivilegeFlags)privs->Privileges[i].Attributes);
		}

		return ret;
	}

	void UserToken::EnablePrivilege(TokenPrivilege^ priv, bool enable)
	{
		typed_buffer_ptr<TOKEN_PRIVILEGES> privs(sizeof(TOKEN_PRIVILEGES) + sizeof(LUID_AND_ATTRIBUTES));

		privs->PrivilegeCount = 1;
		ULARGE_INTEGER luid;
		
		luid.QuadPart = priv->Luid;

		privs->Privileges[0].Luid.HighPart = luid.HighPart;
		privs->Privileges[0].Luid.LowPart = luid.LowPart;
		privs->Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

		if (!AdjustTokenPrivileges(_token->DangerousGetHandle().ToPointer(), 
			FALSE, privs, (DWORD)privs.size(), nullptr, nullptr))
		{
			throw gcnew System::ComponentModel::Win32Exception(::GetLastError());
		}
		else if (::GetLastError() != ERROR_SUCCESS)
		{
			throw gcnew System::ComponentModel::Win32Exception(::GetLastError());
		}
	}
}
