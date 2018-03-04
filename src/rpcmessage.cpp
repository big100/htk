// Copyright (c) 2015-2017 The HTK developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "core_io.h"
#include "init.h"
#include "net.h"
#include "netbase.h"
#include "rpcserver.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include "walletdb.h"

#include <stdint.h>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include <boost/assign/list_of.hpp>

static const CAmount MESSAGE_FEE = 100000;

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

void WalletMessageToJSON(const CWalletTx& wtx, Object& entry)
{
    int confirms = wtx.GetDepthInMainChain(false);
    int confirmsTotal = GetIXConfirmations(wtx.GetHash()) + confirms;
    entry.push_back(Pair("confirmations", confirmsTotal));
    entry.push_back(Pair("bcconfirmations", confirms));
    if (confirms > 0) {
        entry.push_back(Pair("blockhash", wtx.hashBlock.GetHex()));
        entry.push_back(Pair("blockindex", wtx.nIndex));
        entry.push_back(Pair("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime()));
    }
    uint256 hash = wtx.GetHash();
    entry.push_back(Pair("txid", hash.GetHex()));
    Array conflicts;
    BOOST_FOREACH (const uint256& conflict, wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.push_back(Pair("time", wtx.GetTxTime()));
    entry.push_back(Pair("timereceived", (int64_t)wtx.nTimeReceived));
    BOOST_FOREACH (const PAIRTYPE(string, string) & item, wtx.mapValue)
        entry.push_back(Pair(item.first, item.second));
}

string ReadStr(const CScript::const_iterator itbegin, const CScript::const_iterator itend) {
	string rv;
	rv.reserve(itend - itbegin);
	for (CScript::const_iterator it = itbegin; it < itend; ++it) {
        unsigned char val = (unsigned char)(*it);
        rv.push_back(val);
    }
	return rv;
}

string GetMessage(const CScript& script)
{
    string ret;
    CScript::const_iterator it = script.begin();
    opcodetype op;
    while (it != script.end()) {
        CScript::const_iterator it2 = it;
        vector<unsigned char> vch;
        if (script.GetOp2(it, op, &vch)) {
			if (op == OP_RETURN) {
				continue;
			}
            if (vch.size() > 0) {
                ret = ReadStr(it - vch.size(), it);
				break;
            } else {
                ret = ReadStr(it2, it);
				break;
            }
            continue;
        }
        break;
    }
    return ret;
}

void ListMessages(const CWalletTx& wtx, const string& strMessage, bool fLong, Array& ret)
{
	Object entry;
	entry.push_back(Pair("message", strMessage.c_str()));
	if (fLong)
		WalletMessageToJSON(wtx, entry);
	ret.push_back(entry);
}

Value listmessages(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "listmessages (count from) # Temporary Command; Now show all messages \n"
            "\nReturns up to 'count' most recent messages skipping the first 'from' messages for account 'account'.\n"
            "\nArguments:\n"
            "1. count          (numeric, optional, default=10) The number of messages to return\n"
            "2. from           (numeric, optional, default=0) The number of messages to skip\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"message\":\"hiddenmessage\",    (string) The received the message.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the message.\n"
            "    \"bcconfirmations\": n,     (numeric) The number of blockchain confirmations for the message.\n"
            "    \"blockhash\": \"hashvalue\", (string) The block hash containing the message's transaction.\n"
            "    \"blockindex\": n,          (numeric) The block index containing the message's transaction.\n"
            "    \"txid\": \"transactionid\", (string) The transaction id.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (midnight Jan 1 1970 GMT).\n"
            "  }\n"
            "]\n"

            "\nExamples:\n"
            "\nList the most recent 10 messages in the systems\n" +
            HelpExampleCli("listmessages", "") +
            "\nList the most recent 10 messages\n" + HelpExampleCli("listmessages", "") +
            "\nList messages 100 to 120\n" + HelpExampleCli("listmessages", "20 100") +
            "\nAs a json rpc call\n" + HelpExampleRpc("listmessages", "20, 100"));

    int nCount = 10;
    if (params.size() > 0)
        nCount = params[0].get_int();
    int nFrom = 0;
    if (params.size() > 1)
        nFrom = params[1].get_int();

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    Array ret;

    std::list<CAccountingEntry> acentries;
    CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, "*");

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
        CWalletTx* const pwtx = (*it).second.first;
        if (pwtx != 0) {
			unsigned char opcode = pwtx->vout[0].scriptPubKey[0];
			if(opcode == OP_RETURN) {
				string strMessage = GetMessage(pwtx->vout[0].scriptPubKey);
				ListMessages(*pwtx, strMessage, true, ret);
			}
		}

        if ((int)ret.size() >= (nCount + nFrom)) break;
    }
    // ret is newest to oldest

    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;
    Array::iterator first = ret.begin();
    std::advance(first, nFrom);
    Array::iterator last = ret.begin();
    std::advance(last, nFrom + nCount);

    if (last != ret.end()) ret.erase(last, ret.end());
    if (first != ret.begin()) ret.erase(ret.begin(), first);

    std::reverse(ret.begin(), ret.end()); // Return oldest to newest

    return ret;
}

CBitcoinAddress GetSendingAddress() {
    CWalletDB walletdb(pwalletMain->strWalletFile);

    CAccount account;
	string strAccount("_htksend");
    walletdb.ReadAccount(strAccount, account);

    // Generate a new key
    if (!account.vchPubKey.IsValid()) {
        if (!pwalletMain->GetKeyFromPool(account.vchPubKey))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

        pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, "receive");
        walletdb.WriteAccount(strAccount, account);
    }
    return CBitcoinAddress(account.vchPubKey.GetID());
}

void SendMessage(const string& strMessage, CWalletTx& wtxNew, bool fUseIX = false)
{
	// Check amount
    if (strMessage.length() <= 0 || strMessage.length() >= 280)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid message length");

    if (MESSAGE_FEE > pwalletMain->GetBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    string strError;
    if (pwalletMain->IsLocked()) {
        strError = "Error: Wallet locked, unable to create transaction!";
        LogPrintf("SendMessage() : %s", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // Parse HTK address
    CBitcoinAddress address = GetSendingAddress();
    CScript scriptPubKey = GetScriptForDestination(address.Get());
	
	// Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    if (!pwalletMain->CreateTransaction(scriptPubKey, MESSAGE_FEE, wtxNew, reservekey, nFeeRequired, strError, NULL, ALL_COINS, fUseIX, (CAmount)0)) {
        if (MESSAGE_FEE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("SendMessage() : %s\n", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey, (!fUseIX ? "tx" : "ix")))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

	CWalletTx wtxMsg;
	wtxMsg.vin.push_back(CTxIn(wtxNew.GetHash(), 0));
    
    // Parse HTK address
	std::vector<unsigned char> msgVec;
	for(int i=0; i<strMessage.length(); i++) {
		msgVec.push_back(strMessage.c_str()[i]);
	}
	CScript scriptMessage = CScript() << OP_RETURN << msgVec;
	vector<pair<CScript, CAmount> > vecSend;
	vecSend.push_back(make_pair(scriptMessage, 0));
	// Create and send the message transaction
	if (!pwalletMain->CreateMessageTransaction(wtxNew, MESSAGE_FEE, vecSend, wtxMsg, reservekey, nFeeRequired, strError)) {
        if (nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("SendMessage() : %s\n", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
	if (!pwalletMain->CommitTransaction(wtxMsg, reservekey, (!fUseIX ? "tx" : "ix")))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
}

CAmount GetAccountBalanceFromWallet(CWalletDB& walletdb, const string& strAccount, int nMinDepth, const isminefilter& filter)
{
    CAmount nBalance = 0;

    // Tally wallet transactions
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it) {
        const CWalletTx& wtx = (*it).second;
        if (!IsFinalTx(wtx) || wtx.GetBlocksToMaturity() > 0 || wtx.GetDepthInMainChain() < 0)
            continue;

        CAmount nReceived, nSent, nFee;
        wtx.GetAccountAmounts(strAccount, nReceived, nSent, nFee, filter);

        if (nReceived != 0 && wtx.GetDepthInMainChain() >= nMinDepth)
            nBalance += nReceived;
        nBalance -= nSent + nFee;
    }

    // Tally internal accounting entries
    nBalance += walletdb.GetAccountCreditDebit(strAccount);

    return nBalance;
}

Value sendmessagefrom(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "sendmessagefrom \"fromaccount\" \"message\"\n"
            "\nSent an message using funds from an account to a htk address.\n" +
            HelpRequiringPassphrase() + "\n"
                                        "\nArguments:\n"
                                        "1. \"fromaccount\"       (string, required) The name of the account to send funds from. May be the default account using \"\".\n"
                                        "2. \"message\"           (string, required) A messageto send.\n"
                                        "\nResult:\n"
                                        "\"transactionid\"        (string) The transaction id.\n"
                                        "\nExamples:\n"
                                        "\nSend \"hello htk\" from the default account to the address.\n" +
            HelpExampleCli("sendmessagefrom", "\"\" \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" \"hello htk\"") +
            "\nSend \"hello htk\" from the tabby account to the given address.\n" + HelpExampleCli("sendmessagefrom", "\"tabby\" \"hello htk\"") +
            "\nAs a json rpc call\n" + HelpExampleRpc("sendmessagefrom", "\"tabby\", \"hello htk\""));

	string strAccount = params[0].get_str();
    if (strAccount == "*")
        throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
    string strMessage = params[1].get_str();
    int nMinDepth = 1;
	CAmount nCost = 1;

	CWalletTx wtx;
	wtx.strFromAccount = strAccount;
    EnsureWalletIsUnlocked();

    // Check funds
	CWalletDB walletdb(pwalletMain->strWalletFile);
    CAmount nBalance = GetAccountBalanceFromWallet(walletdb, strAccount, nMinDepth, ISMINE_SPENDABLE);
    if (MESSAGE_FEE > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds (0.001 HTK required)");

	SendMessage(strMessage, wtx);

    return wtx.GetHash().GetHex();
}

Value sendmessage(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "sendmessage \"message\"\n"
            "\nSend an message to a given address.\n" +
            HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1. \"message\"     (string, required) The amount in btc to send. eg 0.1\n"
            "\nResult:\n"
            "\"transactionid\"  (string) The transaction id.\n"
            "\nExamples:\n" +
            HelpExampleCli("sendmessage", "\"hello htk\""));

    // Message
    string strMessage = params[0].get_str();

	CWalletTx wtx;
    EnsureWalletIsUnlocked();

	SendMessage(strMessage, wtx);

    return wtx.GetHash().GetHex();
}
