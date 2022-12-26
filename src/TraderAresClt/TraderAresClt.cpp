/*!
 * \file TraderAresClt.cpp
 * \project	WonderTrader
 *
 * \author Suuuuunseeker
 * \date 2022/08/11
 *
 * \brief
 */
#include "TraderAresClt.h"

#include "../Includes/WTSError.hpp"
#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/WTSSessionInfo.hpp"
#include "../Includes/WTSTradeDef.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Includes/WTSVariant.hpp"
#include "../Includes/IBaseDataMgr.h"

#include "../Share/ModuleHelper.hpp"
#include "../Share/decimal.h"

#include <boost/filesystem.hpp>
#include <iostream>

#include <boost/thread/thread.hpp>

const char* ENTRUST_SECTION = "entrusts";
const char* ORDER_SECTION = "orders";

//By Wesley @ 2022.01.05
#include "../Share/fmtlib.h"
template<typename... Args>
inline void write_log(ITraderSpi* sink, WTSLogLevel ll, const char* format, const Args&... args)
{
	if (sink == NULL)
		return;

	const char* buffer = fmtutil::format(format, args...);

	sink->handleTraderLog(ll, buffer);
}

extern "C"
{
	EXPORT_FLAG ITraderApi* createTrader()
	{
		TraderAresClt *instance = new TraderAresClt();
		return instance;
	}

	EXPORT_FLAG void deleteTrader(ITraderApi* &trader)
	{
		if (NULL != trader)
		{
			delete trader;
			trader = NULL;
		}
	}
}

inline char wrapDirectionType(WTSDirectionType dirType, WTSOffsetType offsetType)
{
	if (WDT_LONG == dirType)
		if (offsetType == WOT_OPEN)
			return '0';
		else
			return '1';
	else
		if (offsetType == WOT_OPEN)
			return '1';
		else
			return '0';
}

//inline char wrapPosDirType(WTSDirectionType dirType)
//{
//	if (WDT_LONG == dirType)
//		return '2';
//	else if (WDT_SHORT == dirType)
//		return '3';
//}
//
//inline WTSDirectionType wrapPosDirType(char dirType)
//{
//	if ('2' == dirType)
//		return WDT_LONG;
//	else if ('3' == dirType)
//		return WDT_SHORT;
//	else
//		return WDT_NET;
//}

inline WTSDirectionType wrapDirectionType(char dirType, char offsetType)
{
	if ('0' == dirType)  // 买
		if (offsetType == '0')  // 开仓
			return WDT_LONG;
		else
			return WDT_SHORT;
	else  // 卖
		if (offsetType == '0')  // 开仓
			return WDT_SHORT;
		else
			return WDT_LONG;
}

inline WTSDirectionType wrapPosDirection(char dirType)
{
	if ('2' == dirType)  // 多头
		return WDT_LONG;
	else  // 空头
		return WDT_SHORT;
}

inline char wrapOffsetType(WTSOffsetType offType)
{
	if (WOT_OPEN == offType)
		return '0';
	else if (WOT_CLOSE == offType)
		return '1';
	else if (WOT_CLOSETODAY == offType)
		return '3';
	else if (WOT_CLOSEYESTERDAY == offType)
		return '4';
	else
		return '2';
}

inline WTSOffsetType wrapOffsetType(char offType)
{
	if ('0' == offType)
		return WOT_OPEN;
	else if ('1' == offType)
		return WOT_CLOSE;
	else if ('3' == offType)
		return WOT_CLOSETODAY;
	else if ('4' == offType)
		return WOT_CLOSEYESTERDAY;
	else
		return WOT_FORCECLOSE;
}

inline int wrapPriceType(WTSPriceType priceType, bool isCFFEX /* = false */)
{
	if (WPT_ANYPRICE == priceType)
		return isCFFEX ? 'I' : '1';
	else if (WPT_LIMITPRICE == priceType)
		return isCFFEX ? 'B' : '2';
	else if (WPT_BESTPRICE == priceType)
		return 'F';
	else
		return 'L';
}

inline WTSPriceType wrapPriceType(char priceType)
{
	if ('1' == priceType || 'C' == priceType || 'D' == priceType || 'E' == priceType)
		return WPT_ANYPRICE;
	else if ('2' == priceType || 'A' == priceType || 'B' == priceType)
		return WPT_LIMITPRICE;
	else if ('L' == priceType || 'M' == priceType || 'N' == priceType)
		return WPT_LASTPRICE;
	else
		return WPT_BESTPRICE;
}

//inline WTSOrderState wrapOrderState(char orderState)
//{
//	if (orderState == '1')
//		return WOS_AllTraded;
//	else if (orderState == '2')
//		return WOS_Submitting;
//	else if (orderState == '3')
//		return WOS_NotTraded_Queuing;
//	else if (orderState == '4')
//		return WOS_Cancelling;
//	else if (orderState == '5')
//		return WOS_Canceled;
//	else
//		return WOS_Nottouched;
//}

inline WTSOrderState wrapOrderState(char orderState)
{
	if (orderState == '1')  // 已成交
		return WOS_AllTraded;
	else if (orderState == '2')  // 待报
		return WOS_NotTraded_NotQueuing;
	else if (orderState == '3')  // 已报
		return WOS_NotTraded_Queuing;
	else if (orderState == '4')  // 待撤
		return WOS_NotTraded_NotQueuing;
	else if (orderState == '5')  // 已撤
		return WOS_Canceled;
	else if (orderState == 'a')  // 废单
		return WOS_Canceled;
	else  // 其他
		return WOS_Submitting;
}

inline uint32_t makeRefID()
{
	static std::atomic<uint32_t> auto_refid(0);
	if (auto_refid == 0)
	{
		//auto_refid = (uint32_t)((TimeUtils::getLocalTimeNow() - TimeUtils::makeTime(20221201, 0)) / 1000 * 10);
		uint32_t curDate = TimeUtils::getCurDate();
		curDate = curDate / 100 * 100 + 1;
		auto_refid = (uint32_t)((TimeUtils::getLocalTimeNow() - TimeUtils::makeTime(curDate, 0)) / 1000 * 50);
	}
	return auto_refid.fetch_add(1);
}

TraderAresClt::TraderAresClt()
	: m_pUserAPI(NULL)
	, m_pApp(NULL)
	, m_mapPosition(NULL)
	, m_ayOrders(NULL)
	, m_ayTrades(NULL)
	, m_ayPosDetail(NULL)
	, m_wrapperState(WS_NOTLOGIN)
	, m_uLastQryTime(0)
	, m_iRequestID(0)
	, m_bscSink(NULL)
	, m_bInQuery(false)
	, m_bStopped(NULL)
	, m_lastQryTime(0)
	, m_pfnCreateObj(NULL)
	, m_pfnDestroyObj(NULL)
	, m_orderRef(makeRefID())
{

}


TraderAresClt::~TraderAresClt()
{

}


bool TraderAresClt::init(WTSVariant* params)
{
	m_nLine = params->getInt32("line");
	m_strUserType = params->get("user_type")->asCString();
	m_strUserID = params->get("user")->asCString();
	m_strInvestorID = params->get("investor_id")->asCString();
	m_strPasswd = params->get("pass")->asCString();

	m_strIP = params->get("IP")->asCString();
	m_nPort = params->getInt32("Port");

	m_strFlowDir = params->getCString("flowdir");

	if (m_strFlowDir.empty())
		m_strFlowDir = "AresTDFlow";

	m_lDate = TimeUtils::getCurDate();

	m_strFlowDir = StrUtil::standardisePath(m_strFlowDir);

	WTSVariant* param = params->get("aresmodule");
	if (param != NULL)
		m_strModule = getBinDir() + DLLHelper::wrap_module(param->asCString(), "");
	else
		m_strModule = getBinDir() + DLLHelper::wrap_module("ErebusTdCpp", "");

	m_hInstAres = DLLHelper::load_library(m_strModule.c_str());

	m_pfnCreateObj = (pfnCreateObj)::GetProcAddress(m_hInstAres, "CreateTdObj");
	m_pfnDestroyObj = (pfnDestroyObj)::GetProcAddress(m_hInstAres, "DestroyTdObj");

	return true;
}

void TraderAresClt::release()
{
	if (m_pUserAPI)
	{
		m_pUserAPI->RegisterSpi(NULL);
		m_pUserAPI->ReqLogout();
		FreeApi(m_pUserAPI);
		//m_pUserAPI = NULL;
	}

	if (m_pApp)
	{
		m_pApp->EndWork();
		FreeApp(m_pApp);
		//m_pApp = NULL;
	}

	if (m_hInstAres)
	{
		::FreeLibrary(m_hInstAres);
		m_pfnCreateObj = 0;
		m_pfnDestroyObj = 0;
		m_hInstAres = 0;
	}

	if (m_ayOrders)
		m_ayOrders->clear();

	if (m_ayPosDetail)
		m_ayPosDetail->clear();

	if (m_mapPosition)
		m_mapPosition->clear();

	if (m_ayTrades)
		m_ayTrades->clear();
}

void TraderAresClt::connect()
{
	if (m_pfnCreateObj)
	{
		// 创建环境
		void* p = m_pfnCreateObj("ErebusTdAPP", 0);
		if (p)
		{
			m_pApp = (ITDErebusApp*)p;
		}

		// 创建API
		char szParam[255] = { 0 };
		_snprintf(szParam, 254, "{IP:%s;Port:%d}", m_strIP.c_str(), m_nPort);
		p = m_pfnCreateObj("ErebusTdApi", szParam);
		if (p)
		{
			m_pUserAPI = (ITDErebusApi*)p;
		}
	}

	m_pUserAPI->RegisterSpi(this);

	if (m_pApp)
	{
		int iRet = m_pApp->BeginWork(0);

		if (iRet < 0)
		{
			// 启动失败
			write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Creating environment failed, code: {}", iRet);
		}
	}

	if (m_thrdWorker == NULL)
	{
		boost::asio::io_service::work work(_asyncio);
		m_thrdWorker.reset(new StdThread([this]() {
			while (true)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				_asyncio.run_one();
				//m_asyncIO.run();
			}
		}));
	}

	_asyncio.post([this]() {
		if (m_bscSink)
			m_bscSink->handleEvent(WTE_Connect, 0);
	});
}

void TraderAresClt::disconnect()
{
	//std::cout << "disconnect ..." << std::endl;

	//if (m_thrdWorker)
	//{
	//	m_thrdWorker->join();
	//	m_thrdWorker = NULL;
	//}

	release();
}

bool TraderAresClt::makeEntrustID(char* buffer, int length)
{
	if (buffer == NULL || length == 0)
		return false;

	try
	{
		memset(buffer, 0, length);
		uint32_t orderref = m_orderRef.fetch_add(1) + 1;
		fmtutil::format_to(buffer, "{}#{}#{}", m_strUserID, m_nLine, orderref);
		//write_log(m_bscSink, LL_INFO, "[makeEntrustID] buffer: {}, orderref: {}", buffer, orderref);
		return true;
	}
	catch (...)
	{

	}

	return false;
}

void TraderAresClt::registerSpi(ITraderSpi *listener)
{
	m_bscSink = listener;
	if (m_bscSink)
	{
		m_bdMgr = listener->getBaseDataMgr();
	}
}

uint32_t TraderAresClt::genRequestID()
{
	return m_iRequestID.fetch_add(1) + 1;
}

int TraderAresClt::login(const char* user, const char* pass, const char* productInfo)
{
	m_strUserID = user;
	m_strPasswd = pass;

	if (m_pUserAPI == NULL)
	{
		return -1;
	}

	doLogin();

	return 0;
}

int TraderAresClt::doLogin()
{
	m_wrapperState = WS_LOGINING;

	tagXTReqUserLoginField req = { 0 };
	{
		memset(&req, 0, sizeof(req));

		req.Line = m_nLine;
		req.UserType[0] = m_strUserType.at(0);

		strcpy(req.UserID, m_strUserID.c_str());
		strcpy(req.InvestorID, m_strInvestorID.c_str());
		strcpy(req.Password, m_strPasswd.c_str());  // 使用资金账户密码登录
	}

	int iResult = m_pUserAPI->ReqLogon(&req);
	if (iResult <= 0)
	{
		std::string msg = m_strErrInfo;

		write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Sending login request failed: {}", msg);
		m_wrapperState = WS_LOGINFAILED;

		_asyncio.post([this, msg] {
			m_bscSink->onLoginResult(false, msg.c_str(), 0);
		});
	}
	else
	{
		write_log(m_bscSink, LL_INFO, "[TraderAresClt] Sending login request successed: {}", iResult);

		//m_wrapperState = WS_LOGINED;

		//_asyncio.post([this] {
		//	m_wrapperState = WS_ALLREADY;
		//	m_bscSink->onLoginResult(true, 0, m_lDate);
		//});
	}

	return 0;
}

int TraderAresClt::logout()
{
	if (m_pUserAPI == NULL)
	{
		return -1;
	}

	m_pUserAPI->ReqLogout();

	return 0;
}

int TraderAresClt::orderInsert(WTSEntrust* entrust)
{
	if (m_pUserAPI == NULL || m_wrapperState != WS_ALLREADY)
	{
		return -1;
	}

	WTSContractInfo* ct = entrust->getContractInfo();
	if (ct != NULL)
	{
		tagXTReqOrderInsertField req;
		memset(&req, 0, sizeof(req));

		///用户代码
		req.Line = m_nLine;
		req.UserType[0] = m_strUserType.at(0);
		strcpy(req.UserID, m_strUserID.c_str());
		///投资者代码
		strcpy(req.InvestorID, m_strInvestorID.c_str());
		///合约代码
		strcpy(req.Code, entrust->getCode());
		/// 交易所代码
		strcpy(req.Exchange, entrust->getExchg());

		if (strlen(entrust->getUserTag()) == 0)
		{
			///报单引用
			fmt::format_to(req.OrderRef, "{}", m_orderRef.fetch_add(0));
		}
		else
		{
			uint32_t orderref;
			extractEntrustID(entrust->getEntrustID(), orderref);
			///报单引用
			fmt::format_to(req.OrderRef, "{}", orderref);
		}

		if (strlen(entrust->getUserTag()) > 0)
		{
			//m_mapEntrustTag[entrust->getEntrustID()] = entrust->getUserTag();
			m_iniHelper.writeString(ENTRUST_SECTION, entrust->getEntrustID(), entrust->getUserTag());
			m_iniHelper.save();

			//m_eidCache.put(entrust->getEntrustID(), entrust->getUserTag());
		}

		WTSCommodityInfo* commInfo = ct->getCommInfo();

		///用户代码
		///报单价格条件: 限价
		req.PriceType[0] = wrapPriceType(entrust->getPriceType(), strcmp(commInfo->getExchg(), "CFFEX") == 0);
		///买卖方向: 
		req.Direction[0] = wrapDirectionType(entrust->getDirection(), entrust->getOffsetType());
		///组合开平标志: 开仓
		req.Offset[0] = wrapOffsetType(entrust->getOffsetType());
		///组合投机套保标志
		req.Hedge[0] = '1';  // 投机
		///价格
		req.LimitPrice = entrust->getPrice();
		///数量: 1
		req.VolumeOrigin = (int)entrust->getVolume();

		/*std::cout << "Code: " << req.Code << "  Exchange: " << req.Exchange << "  PriceType: " << req.PriceType << "  Offset: " << req.Offset
			<< "  LimitPrice: " << req.LimitPrice << "  Volume: " << req.VolumeOrigin << "  Direction: " << req.Direction << "  OrderRef: " << req.OrderRef << std::endl;*/
		
		int iResult = m_pUserAPI->ReqOrderInsert(&req);
		if (iResult <= 0)
		{
			write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Order inserting failed: {}", iResult);
		}
		else
		{
			write_log(m_bscSink, LL_INFO, "[OrderInsert] code: {}, volume: {}, price: {}, orderref: {}, usertag: {}, entrustid: {}", req.Code, req.VolumeOrigin, req.LimitPrice, req.OrderRef, entrust->getUserTag(), entrust->getEntrustID());
		}

		return 0;
	}

	write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Order inserting failed: Invalid contract!");
	return -1;
}

int TraderAresClt::orderAction(WTSEntrustAction* action)
{
	if (m_wrapperState != WS_ALLREADY)
		return -1;


	uint32_t orderref;

	if (extractEntrustID(action->getEntrustID(), orderref))
	{
		tagXTReqOrderCancelField req;
		memset(&req, 0, sizeof(req));
		///经纪公司代码
		wt_strcpy(req.UserID, m_strUserID.c_str(), m_strUserID.size());
		wt_strcpy(req.InvestorID, m_strInvestorID.c_str(), m_strInvestorID.size());
		///报单引用
		fmt::format_to(req.ActionRef, "{}", orderref);
		///请求编号
		strcpy(req.OrderSysID, action->getOrderID());
		///合约代码
		strcpy(req.Code, action->getCode());
		strcpy(req.Exchange, action->getExchg());

		write_log(m_bscSink, LL_INFO, "[orderAction]code: {}, actionRef: {}, orderSysID: {}, entrustid: {}", req.Code, req.ActionRef, req.OrderSysID, action->getEntrustID());

		int iResult = m_pUserAPI->ReqOrderCancel(&req);
		if (iResult <= 0)
		{
			write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Sending cancel request failed: {}", iResult);
		}

		return 0;
	}

	return -1;
}

int TraderAresClt::queryAccount()
{
	if (m_pUserAPI == NULL || m_wrapperState != WS_ALLREADY)
	{
		write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Trading api is null or system is not all ready");
		return -1;
	}

	tagXTReqQryAccountField req;
	memset(&req, 0, sizeof(req));

	req.Line = m_nLine;
	req.UserType[0] = m_strUserType.at(0);
	strncpy(req.UserID, m_strUserID.c_str(), sizeof(req.UserID));
	strncpy(req.InvestorID, m_strInvestorID.c_str(), sizeof(req.InvestorID));
	int iRet = m_pUserAPI->ReqQryAccount(&req);

	if (iRet <= 0)
	{
		write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Account querying failed: {}", m_strErrInfo);
	}

	//StdUniqueLock lock(m_mtxQuery);
	//m_queQuery.push([this]() {
	//	tagXTReqQryAccountField req;
	//	memset(&req, 0, sizeof(req));

	//	req.Line = m_nLine;
	//	req.UserType[0] = m_strUserType.at(0);
	//	strncpy(req.UserID, m_strUserID.c_str(), sizeof(req.UserID));
	//	strncpy(req.InvestorID, m_strInvestorID.c_str(), sizeof(req.InvestorID));
	//	int iRet = m_pUserAPI->ReqQryAccount(&req);
	//	if (iRet <= 0)
	//{
	//	std::cout << "ReqQryAccount Error: " << m_strErrInfo << std::endl;
	//}
	//});

	return 0;
}

int TraderAresClt::queryPositions()
{
	if (m_pUserAPI == NULL || m_wrapperState != WS_ALLREADY)
	{
		write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Trading api is null or system is not all ready");
		return -1;
	}

	tagXTReqQryPositionField req;
	memset(&req, 0, sizeof(req));

	strcpy(req.UserID, m_strUserID.c_str());
	strcpy(req.InvestorID, m_strInvestorID.c_str());
	int iRet = m_pUserAPI->ReqQryPosition(&req);

	if (iRet <= 0)
		write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Failed to query position info, Error: {}", m_strErrInfo);
	else
		write_log(m_bscSink, LL_INFO, "[TraderAresClt] Succeeded to query position info, Code: {}", iRet);

	//StdUniqueLock lock(m_mtxQuery);
	//m_queQuery.push([this]() {
	//	tagXTReqQryPositionField req;
	//	memset(&req, 0, sizeof(req));

	//	strcpy(req.UserID, m_strUserID.c_str());
	//	strcpy(req.InvestorID, m_strInvestorID.c_str());
	//	int iRet = m_pUserAPI->ReqQryPosition(&req);
	//	if (iRet <= 0)
	//	{
	//		std::cout << "ReqQryPosition Error: " << m_strErrInfo << std::endl;
	//	}
	//});

	return 0;
}

int TraderAresClt::queryOrders()
{
	if (m_pUserAPI == NULL || m_wrapperState != WS_ALLREADY)
	{
		return -1;
	}

	tagXTReqQryOrderField req;
	memset(&req, 0, sizeof(req));
	strcpy(req.UserID, m_strUserID.c_str());
	strcpy(req.InvestorID, m_strInvestorID.c_str());

	int iRet = m_pUserAPI->ReqQryOrder(&req);
	if (iRet <= 0)
	{
		write_log(m_bscSink, LL_ERROR, "[TraderAresClt]ReqQryOrder failed, {}", m_strErrInfo);
	}

	//StdUniqueLock lock(m_mtxQuery);
	//m_queQuery.push([this]() {
	//	tagXTReqQryOrderField req;
	//	memset(&req, 0, sizeof(req));
	//	strcpy(req.UserID, m_strUserID.c_str());
	//	strcpy(req.InvestorID, m_strInvestorID.c_str());

	//	int iRet = m_pUserAPI->ReqQryOrder(&req);

	//	if (iRet <= 0)
	//	{
	//		std::cout << "ReqQryOrder failed: " << m_strErrInfo << std::endl;
	//	}
	//});

	return 0;
}

int TraderAresClt::queryTrades()
{
	if (m_pUserAPI == NULL || m_wrapperState != WS_ALLREADY)
	{
		return -1;
	}

	tagXTReqQryTradeField req;
	memset(&req, 0, sizeof(req));
	strcpy(req.UserID, m_strUserID.c_str());
	strcpy(req.InvestorID, m_strInvestorID.c_str());

	int iRet = m_pUserAPI->ReqQryTrade(&req);
	if (iRet <= 0)
	{
		write_log(m_bscSink, LL_ERROR, "[TraderAresClt]ReqQryTrade failed, {}", m_strErrInfo);
	}

	//StdUniqueLock lock(m_mtxQuery);
	//m_queQuery.push([this]() {
	//	tagXTReqQryTradeField req;
	//	memset(&req, 0, sizeof(req));
	//	strcpy(req.UserID, m_strUserID.c_str());
	//	strcpy(req.InvestorID, m_strInvestorID.c_str());

	//	int iRet = m_pUserAPI->ReqQryTrade(&req);

	//	if (iRet <= 0)
	//	{
	//		std::cout << "ReqQryTrade failed: " << m_strErrInfo << std::endl;
	//	}
	//});

	return 0;
}

int TraderAresClt::querySettlement(uint32_t uDate)
{
	if (m_pUserAPI == NULL || m_wrapperState != WS_ALLREADY)
	{
		return -1;
	}

	write_log(m_bscSink, LL_ERROR, "[TraderAresClt] Begin to query settlement info ...");

	_asyncio.post([this, uDate]() {
		if (m_bscSink)
			m_bscSink->onRspSettlementInfo(uDate, "No settlement data");
	});

	////m_strSettleInfo.clear();
	//StdUniqueLock lock(m_mtxQuery);
	//m_queQuery.push([this, uDate]() {
	//	/*CThostFtdcQrySettlementInfoField req;
	//	memset(&req, 0, sizeof(req));
	//	strcpy(req.BrokerID, m_strBroker.c_str());
	//	strcpy(req.InvestorID, m_strUser.c_str());
	//	fmt::format_to(req.TradingDay, "{}", uDate);

	//	m_pUserAPI->ReqQrySettlementInfo(&req, genRequestID());*/

	//});

	//triggerQuery();

	return 0;
}

bool TraderAresClt::IsErrorRspInfo(tagXTRspInfoField *pRspInfo)
{
	if (pRspInfo && pRspInfo->ErrorID != 0)
		return true;

	return false;
}

void TraderAresClt::OnFrontConnected()
{
	/// 先调用ReLoginOn，才会进行网络连接，连接成功后回调此函数
	write_log(m_bscSink, LL_INFO, "[TraderAresClt] Server connected");
}

void TraderAresClt::OnFrontDisconnected()
{
	m_wrapperState = WS_NOTLOGIN;
	if (m_bscSink)
		m_bscSink->handleEvent(WTE_Close, -1);

	// 断开重连
	_asyncio.post([this]() {
		write_log(m_bscSink, LL_WARN, "[TraderAresClt] Connection lost, relogin in 2 seconds...");
		std::this_thread::sleep_for(std::chrono::seconds(2));
		doLogin();
	});
}

void TraderAresClt::OnRspUserLogin(tagXTRspUserLoginField* pRspUserLogin, tagXTRspInfoField* error, int id, bool last)
{
	if (error)
		m_strErrInfo = error->ErrorMsg;

	if (!IsErrorRspInfo(error))
	{
		m_wrapperState = WS_LOGINED;

		{
			///获取当前交易日
			m_lDate = TimeUtils::getCurDate();

			//{
			//	// 初始化委托订单缓存器
			//	std::stringstream ss;
			//	ss << "local/" << m_strUserID << "/";
			//	std::string path = StrUtil::standardisePath(ss.str());
			//	if (!StdFile::exists(path.c_str()))
			//		boost::filesystem::create_directories(path.c_str());
			//	ss << m_strUserID << "_eid.sc";

			//	m_eidCache.init(ss.str().c_str(), m_lDate, [this](const char* message)
			//	{
			//		write_log(m_bscSink, LL_WARN, message);
			//	});
			//}

			//{
			//	//初始化订单标记缓存器
			//	std::stringstream ss;
			//	ss << m_strFlowDir << "local/" << m_strUserID << "/";
			//	std::string path = StrUtil::standardisePath(ss.str());
			//	if (!StdFile::exists(path.c_str()))
			//		boost::filesystem::create_directories(path.c_str());
			//	ss << m_strUserID << "_oid.sc";
			//	m_oidCache.init(ss.str().c_str(), m_lDate, [this](const char* message) {
			//		write_log(m_bscSink, LL_WARN, message);
			//	});
			//}

			write_log(m_bscSink, LL_INFO, "[TraderAresClt][{}-{}] Login succeed, sessionid: {}, trading date: {}...", m_strUserID.c_str(), m_strInvestorID.c_str(), m_nLine, m_lDate);

			{
				std::stringstream ss;
				ss << m_strFlowDir << "local/" << m_strUserID << "/";
				std::string path = StrUtil::standardisePath(ss.str());
				if (!StdFile::exists(path.c_str()))
					boost::filesystem::create_directories(path.c_str());
				ss << m_strUserID << ".dat";

				m_iniHelper.load(ss.str().c_str());

				uint32_t lastDate = m_iniHelper.readUInt("marker", "date", 0);
				if (lastDate != m_lDate)
				{
					//交易日不同,清理掉原来的数据
					m_iniHelper.removeSection(ENTRUST_SECTION);
					m_iniHelper.removeSection(ORDER_SECTION);
					m_iniHelper.writeUInt("marker", "date", m_lDate);
					m_iniHelper.save();

					write_log(m_bscSink, LL_INFO, "[TraderAresClt][{}-{}] Trading date changed [{} -> {}], local cache cleared...", m_strUserID.c_str(), m_strInvestorID.c_str(), lastDate, m_lDate);
				}
			}
		}

		m_wrapperState = WS_ALLREADY;

		if (m_bscSink)
			m_bscSink->onLoginResult(true, 0, m_lDate);
	}
	else
	{
		write_log(m_bscSink, LL_ERROR, "[TraderAresClt][{}-{}] Login failed: {}", m_strUserID.c_str(), m_strInvestorID.c_str(), error->ErrorMsg);
		m_wrapperState = WS_LOGINFAILED;

		if (m_bscSink)
			m_bscSink->onLoginResult(false, error->ErrorMsg, 0);
	}
}

void TraderAresClt::OnRspOrderInsert(tagXTReqOrderInsertField* data, tagXTRspInfoField* error, int id, bool last)
{
	if (error)
		m_strErrInfo = error->ErrorMsg;

	WTSEntrust* entrust = makeEntrust(data);

	if (entrust)
	{
		write_log(m_bscSink, LL_INFO, "[OnRspOrderInsert][{}] code: {}, volume: {}, price: {}, id: {}, order ref: {}, usertag: {}, entrustid: {}",
			data->UserID, data->Code, data->VolumeOrigin, data->LimitPrice, id, data->OrderRef, entrust->getUserTag(), entrust->getEntrustID());

		if (IsErrorRspInfo(error))
		{
			WTSError *err = makeError(error, WEC_ORDERINSERT);
			if (m_bscSink)
				m_bscSink->onRspEntrust(entrust, err);
			err->release();
		}
		else
		{
			if (m_bscSink)
				m_bscSink->onRspEntrust(entrust, NULL);
		}
	}
	else if (IsErrorRspInfo(error))
	{
		WTSError *err = makeError(error, WEC_ORDERINSERT);
		if (m_bscSink)
			m_bscSink->onTraderError(err);
		err->release();
	}

	if (entrust)
		entrust->release();
}

void TraderAresClt::OnRspOrderAction(tagXTReqOrderCancelField* data, tagXTRspInfoField* pRspInfo, int id, bool last)
{
	if (pRspInfo)
		m_strErrInfo = pRspInfo->ErrorMsg;

	if (data)
		write_log(m_bscSink, LL_INFO, "[OnRspOrderAction][{}] code: {}, id: {}, order ref: {}", data->UserID, data->Code, id, data->ActionRef);

	if (IsErrorRspInfo(pRspInfo))
	{
		write_log(m_bscSink, LL_INFO, "[TraderAresClt][OnRspOrderAction] Cancell order failed, msg: {}", pRspInfo->ErrorMsg);
		WTSError* error = WTSError::create(WEC_ORDERCANCEL, pRspInfo->ErrorMsg);
		if (m_bscSink)
			m_bscSink->onTraderError(error);
	}
	else
	{
		write_log(m_bscSink, LL_INFO, "[TraderAresClt][OnRspOrderAction] Cancell order successed, msg: {}", pRspInfo->ErrorMsg);
	}
}

void TraderAresClt::OnErrRtnOrderAction(tagXTReqOrderCancelField* orderCancelField, tagXTRspInfoField* pRspInfo)
{
	if (pRspInfo)
	{
		m_strErrInfo = pRspInfo->ErrorMsg;
		write_log(m_bscSink, LL_ERROR, "[OnErrRtnOrderAction] Cancel failed, msg: {}", pRspInfo->ErrorMsg);
	}

	WTSEntrust* entrust = makeEntrust(orderCancelField);
	if (entrust)
	{
		WTSError *err = makeError(pRspInfo, WEC_ORDERCANCEL);
		if (m_bscSink)
			m_bscSink->onRspEntrust(entrust, err);

		entrust->release();
		err->release();
	}
}

void TraderAresClt::OnRspQryTradingAccount(tagXTRspAccountField* pTradingAccount, tagXTRspInfoField* pRspInfo, int id, bool bIsLast)
{
	//write_log(m_bscSink, LL_INFO, "OnRspQryTradingAccount: {}", pRspInfo->ErrorMsg);

	if (pRspInfo)
		m_strErrInfo = pRspInfo->ErrorMsg;

	if (bIsLast)
	{
		m_bInQuery = false;
	}

	if (bIsLast && !IsErrorRspInfo(pRspInfo) && pTradingAccount)
	{
		//std::cout << "Line: " << pTradingAccount->Line << "  UserType: " << pTradingAccount->UserType[0]
		//	<< "  UserID: " << pTradingAccount->UserID << "  InvestorID: " << pTradingAccount->InvestorID
		//	<< "  FrozenCash: " << pTradingAccount->FrozenMargin << "  Available: " << pTradingAccount->Available
		//	<< "  FrozenMargin: " << pTradingAccount->FrozenMargin << std::endl;

		WTSAccountInfo* accountInfo = WTSAccountInfo::create();
		accountInfo->setPreBalance(0);
		accountInfo->setCloseProfit(0);
		accountInfo->setDynProfit(0);
		accountInfo->setMargin(0);
		accountInfo->setAvailable(pTradingAccount->Available);
		accountInfo->setCommission(0);
		accountInfo->setFrozenMargin(pTradingAccount->FrozenMargin);
		//accountInfo->setFrozenCommission(pTradingAccount->FrozenCommission);
		accountInfo->setDeposit(0);
		accountInfo->setWithdraw(0);
		accountInfo->setBalance(accountInfo->getPreBalance() + accountInfo->getCloseProfit() - accountInfo->getCommission() + accountInfo->getDeposit() - accountInfo->getWithdraw());
		accountInfo->setCurrency("CNY");

		WTSArray * ay = WTSArray::create();
		ay->append(accountInfo, false);
		if (m_bscSink)
			m_bscSink->onRspAccount(ay);

		ay->release();
	}
}

void TraderAresClt::OnRspQryInvestorPosition(tagXTRspPositionField* pInvestorPosition, tagXTRspInfoField* pRspInfo, int id, bool bIsLast)
{
	//write_log(m_bscSink, LL_INFO, "Begin to callback result of positions, msg: {}", pRspInfo->ErrorMsg);

	if (pRspInfo)
		m_strErrInfo = pRspInfo->ErrorMsg;

	if (bIsLast)
	{
		m_bInQuery = false;
		//triggerQuery();
	}

	if (!IsErrorRspInfo(pRspInfo) && pInvestorPosition)
	{
		if (NULL == m_mapPosition)
			m_mapPosition = PositionMap::create();

		WTSContractInfo* contract = m_bdMgr->getContract(pInvestorPosition->Code, pInvestorPosition->Exchange);

		//if (contract)
		//{
		//	WTSCommodityInfo* commInfo = contract->getCommInfo();
		//	std::string key = fmt::format("{}-{}", pInvestorPosition->Code, pInvestorPosition->PosiDirection);
		//	WTSPositionItem* pos = (WTSPositionItem*)m_mapPosition->get(key);
		//	if (pos == NULL)
		//	{
		//		pos = WTSPositionItem::create(pInvestorPosition->Code, commInfo->getCurrency(), commInfo->getExchg());
		//		pos->setContractInfo(contract);
		//		m_mapPosition->add(key, pos, false);
		//	}
		//	pos->setDirection(wrapPosDirection(pInvestorPosition->PosiDirection[0]));
		//	if (commInfo->getCoverMode() == CM_CoverToday)
		//	{
		//		//if (pInvestorPosition->PositionDate == THOST_FTDC_PSD_Today)
		//		//	pos->setNewPosition(pInvestorPosition->Position);
		//		//else
		//		//	pos->setPrePosition(pInvestorPosition->Position);

		//		pos->setNewPosition(pInvestorPosition->Position);
		//		pos->setPrePosition(pInvestorPosition->YdPosition);
		//	}
		//	else
		//	{
		//		pos->setNewPosition(pInvestorPosition->Position);
		//		pos->setPrePosition(pInvestorPosition->YdPosition);
		//	}

		//	pos->setMargin(pos->getMargin() + pInvestorPosition->UseMargin);
		//	//pos->setDynProfit(pos->getDynProfit() + pInvestorPosition->PositionProfit);
		//	pos->setPositionCost(pos->getPositionCost() + pInvestorPosition->PositionCost);

		//	if (pos->getTotalPosition() != 0)
		//	{
		//		pos->setAvgPrice(pos->getPositionCost() / pos->getTotalPosition() / commInfo->getVolScale());
		//	}
		//	else
		//	{
		//		pos->setAvgPrice(0);
		//	}

		//	if (commInfo->getCategoty() != CC_Combination)
		//	{
		//		if (commInfo->getCoverMode() == CM_CoverToday)
		//		{
		//			int availNew = pInvestorPosition->Position;

		//			availNew -= pInvestorPosition->FrozenPosition;

		//			if (availNew < 0)
		//				availNew = 0;
		//			pos->setAvailNewPos(availNew);

		//			int availPre = pInvestorPosition->YdPosition;
		//			availPre -= pInvestorPosition->FrozenPosition;

		//			if (availPre < 0)
		//				availPre = 0;
		//			pos->setAvailPrePos(availPre);
		//		}
		//		else
		//		{
		//			int availNew = pInvestorPosition->Position;

		//			availNew -= pInvestorPosition->FrozenPosition;

		//			if (availNew < 0)
		//				availNew = 0;

		//			pos->setAvailNewPos(availNew);

		//			double availPre = pos->getNewPosition() + pos->getPrePosition()
		//				- pInvestorPosition->FrozenPosition - pos->getAvailNewPos();
		//			pos->setAvailPrePos(availPre);
		//		}
		//	}
		//	else
		//	{

		//	}

		//	if (decimal::lt(pos->getTotalPosition(), 0.0) && decimal::eq(pos->getMargin(), 0.0))
		//	{
		//		//有仓位,但是保证金为0,则说明是套利合约,单个合约的可用持仓全部置为0
		//		pos->setAvailNewPos(0);
		//		pos->setAvailPrePos(0);
		//	}

		//	write_log(m_bscSink, LL_INFO, "[OnRspTradePosition][{}]code: {}, position: {}, ydposition: {}, availNewPos: {}, preavailPos: {}, NewPos: {}", pInvestorPosition->UserID, pInvestorPosition->Code, pInvestorPosition->Position, pInvestorPosition->YdPosition, pos->getAvailNewPos(), pos->getAvailPrePos(), pos->getNewPosition());
		//}


		if (contract)
		{
			WTSCommodityInfo* commInfo = contract->getCommInfo();
			std::string key = fmt::format("{}-{}", pInvestorPosition->Code, pInvestorPosition->PosiDirection);
			WTSPositionItem* pos = (WTSPositionItem*)m_mapPosition->get(key);
			if (pos == NULL)
			{
				pos = WTSPositionItem::create(pInvestorPosition->Code, commInfo->getCurrency(), commInfo->getExchg());
				pos->setContractInfo(contract);
				m_mapPosition->add(key, pos, false);
			}
			pos->setDirection(wrapPosDirection(pInvestorPosition->PosiDirection[0]));
			pos->setNewPosition(pInvestorPosition->Position);
			pos->setPrePosition(pInvestorPosition->YdPosition);

			pos->setMargin(pos->getMargin() + pInvestorPosition->UseMargin);
			pos->setPositionCost(pos->getPositionCost() + pInvestorPosition->PositionCost);

			if (pos->getTotalPosition() != 0)
			{
				pos->setAvgPrice(pos->getPositionCost() / pos->getTotalPosition() / commInfo->getVolScale());
			}
			else
			{
				pos->setAvgPrice(0);
			}

			if (commInfo->getCategoty() != CC_Combination)
			{
				int availNew = pInvestorPosition->Position;

				availNew -= pInvestorPosition->FrozenPosition;

				if (availNew < 0)
					availNew = 0;
				pos->setAvailNewPos(availNew);

				int availPre = pInvestorPosition->YdPosition;
				availPre -= pInvestorPosition->FrozenPosition;

				if (availPre < 0)
					availPre = 0;
				pos->setAvailPrePos(availPre);
			}
			else
			{

			}

			if (decimal::lt(pos->getTotalPosition(), 0.0) && decimal::eq(pos->getMargin(), 0.0))
			{
				//有仓位,但是保证金为0,则说明是套利合约,单个合约的可用持仓全部置为0
				pos->setAvailNewPos(0);
				pos->setAvailPrePos(0);
			}

			write_log(m_bscSink, LL_INFO, "[OnRspTradePosition][{}]code: {}, position: {}, ydposition: {}, availNewPos: {}, preavailPos: {}, NewPos: {}", pInvestorPosition->UserID, pInvestorPosition->Code, pInvestorPosition->Position, pInvestorPosition->YdPosition, pos->getAvailNewPos(), pos->getAvailPrePos(), pos->getNewPosition());
		}
	}

	if (bIsLast)
	{
		WTSArray* ayPos = WTSArray::create();

		if (m_mapPosition && m_mapPosition->size() > 0)
		{
			for (auto it = m_mapPosition->begin(); it != m_mapPosition->end(); it++)
			{
				ayPos->append(it->second, true);
			}
		}

		if (m_bscSink)
			m_bscSink->onRspPosition(ayPos);


		if (m_mapPosition)
		{
			m_mapPosition->release();
			m_mapPosition = NULL;
		}

		ayPos->release();
	}
}

void TraderAresClt::OnRspQryTrade(tagXTTradeField *pTrade, tagXTRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (pRspInfo)
		m_strErrInfo = pRspInfo->ErrorMsg;

	if (bIsLast)
	{
		m_bInQuery = false;
		//triggerQuery();
	}

	if (!IsErrorRspInfo(pRspInfo) && pTrade)
	{
		write_log(m_bscSink, LL_INFO, "[OnRspQryTrade][{}]code: {}, direction: {}, offset: {}, price: {}, volume: {}, ordersysid: {}, orderref: {}", pTrade->UserID, pTrade->Code, pTrade->Direction, pTrade->Offset, pTrade->Price, pTrade->Volume, pTrade->OrderSysID, pTrade->OrderRef);

		if (NULL == m_ayTrades)
			m_ayTrades = WTSArray::create();

		WTSTradeInfo* trade = makeTradeRecord(pTrade);
		if (trade)
		{
			m_ayTrades->append(trade, false);
		}
	}

	if (bIsLast)
	{
		if (m_bscSink)
			m_bscSink->onRspTrades(m_ayTrades);

		if (NULL != m_ayTrades)
			m_ayTrades->clear();
	}
}

void TraderAresClt::OnRspQryOrder(tagXTOrderField *pOrder, tagXTRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (pRspInfo)
		m_strErrInfo = pRspInfo->ErrorMsg;

	if (bIsLast)
	{
		m_bInQuery = false;
	}

	if (!IsErrorRspInfo(pRspInfo) && pOrder)
	{
		//std::cout << "Line: " << pOrder->Line << "  UserType: " << pOrder->UserType[0]
		//	<< "  UserID: " << pOrder->UserID << "  InvestorID: " << pOrder->InvestorID
		//	<< "  Exchange: " << pOrder->Exchange << "  Code: " << pOrder->Code
		//	<< "  Direction: " << pOrder->Direction[0] << "  Offset: " << pOrder->Offset[0]
		//	<< "  LimitPrice: " << pOrder->LimitPrice << "  OriginVolume: " << pOrder->VolumeOrigin 
		//	<< "  VolumeTraded: " << pOrder->VolumeTraded << "  VolumeRemain: " << pOrder->VolumeRemain
		//	<< "  OrderStatus: " << pOrder->OrderStatus << "  StatusMsg: " << pOrder->StatusMsg
		//	<< "  InsertDate: " << pOrder->InsertDate << "  InsertTime: " << pOrder->InsertTime << std::endl;

		if (NULL == m_ayOrders)
			m_ayOrders = WTSArray::create();

		WTSOrderInfo* orderInfo = makeOrderInfo(pOrder);
		if (orderInfo)
		{
			m_ayOrders->append(orderInfo, false);
		}
	}

	if (bIsLast)
	{
		if (m_bscSink)
			m_bscSink->onRspOrders(m_ayOrders);

		if (m_ayOrders)
			m_ayOrders->clear();
	}
}

void TraderAresClt::OnRtnOrder(tagXTOrderField *pOrder)
{
	WTSOrderInfo *orderInfo = makeOrderInfo(pOrder);
	if (orderInfo)
	{
		_asyncio.post([this, orderInfo] {
			if (m_bscSink)
				m_bscSink->onPushOrder(orderInfo);

			orderInfo->release();
		});
	}
}

void TraderAresClt::OnRtnTrade(tagXTTradeField *pTrade)
{
	WTSTradeInfo *tRecord = makeTradeRecord(pTrade);
	if (tRecord)
	{
		_asyncio.post([this, tRecord] {
			//std::this_thread::sleep_for(std::chrono::microseconds(15));

			//std::string usertag = m_iniHelper.readString(ORDER_SECTION, StrUtil::trim(tRecord->getRefOrder()).c_str());
			////std::string usertag = m_oidCache.get(StrUtil::trim(pRet->getRefOrder()).c_str());
			//if (!usertag.empty())
			//	tRecord->setUserTag(usertag.c_str());

			if (m_bscSink)
				m_bscSink->onPushTrade(tRecord);

			tRecord->release();
		});
	}
}


WTSOrderInfo* TraderAresClt::makeOrderInfo(tagXTOrderField* orderField)
{
	if (orderField == NULL)
	{
		write_log(m_bscSink, LL_ERROR, "[makeOrderInfo][tagXTReqOrderField] orderField is NULL!");
		return NULL;
	}

	WTSContractInfo* contract = m_bdMgr->getContract(orderField->Code, orderField->Exchange);
	if (contract == NULL)
	{
		write_log(m_bscSink, LL_ERROR, "[makeOrderInfo][tagXTReqOrderField] Contract is NULL! Code: {}, OrderRef: {}, OrderSysID: {}, OrderState: {}", orderField->Code, orderField->OrderRef, orderField->OrderSysID, orderField->OrderStatus);
		return NULL;
	}

	WTSOrderInfo* pRet = WTSOrderInfo::create();
	pRet->setPrice(orderField->LimitPrice);
	pRet->setVolume(orderField->VolumeOrigin);
	pRet->setDirection(wrapDirectionType(orderField->Direction[0], orderField->Offset[0]));
	pRet->setPriceType(wrapPriceType(orderField->PriceType[0]));
	pRet->setOffsetType(wrapOffsetType(orderField->Offset[0]));
	pRet->setContractInfo(contract);

	pRet->setOrderFlag(WOF_NOR);

	pRet->setVolTraded(orderField->VolumeTraded);
	pRet->setVolLeft(orderField->VolumeRemain);

	pRet->setCode(orderField->Code);
	pRet->setExchange(contract->getExchg());

	pRet->setOrderDate(orderField->InsertDate);
	pRet->setOrderTime(orderField->InsertTime);

	pRet->setOrderState(wrapOrderState(orderField->OrderStatus[0]));  // 订单报单状态
	//pRet->setEntrustID(generateEntrustID(orderField->FrontID, orderField->SessionID, atoi(orderField->OrderRef)).c_str());
	generateEntrustID(pRet->getEntrustID(), atoi(orderField->OrderRef));
	pRet->setOrderID(orderField->OrderSysID);

	pRet->setStateMsg(orderField->StatusMsg);

	if (orderField->OrderStatus[0] == 'a')  // 废单
	{
		pRet->setError(true);
		pRet->setStateMsg("Abandoned");
	}
	else if (orderField->OrderStatus[0] == '2')  // 待报
	{
		pRet->setError(true);
		//pRet->setOrderState(WOS_Canceled);
		pRet->setStateMsg("Submit_Waited");
	}
	else if (orderField->OrderStatus[0] == '4')  // 待撤
	{
		pRet->setOrderState(WOS_Cancelling);
		pRet->setStateMsg("Canceling");
	}
	else if (orderField->OrderStatus[0] == '5')  // 已撤
	{
		pRet->setOrderState(WOS_Canceled);
		pRet->setStateMsg("Canceled");
	}
	else
	{

	}

	/*const char* usertag = m_eidCache.get(pRet->getEntrustID());
	if (strlen(usertag) == 0)
	{
		pRet->setUserTag(pRet->getEntrustID());
	}
	else
	{
		pRet->setUserTag(usertag);

		if (strlen(pRet->getOrderID()) > 0)
		{
			m_oidCache.put(StrUtil::trim(std::to_string(pRet->getOrderDate()).c_str()).c_str(), usertag, 0, [this](const char* message) {
				write_log(m_bscSink, LL_ERROR, message);
			});
		}
	}*/

	std::string usertag1 = m_iniHelper.readString(ENTRUST_SECTION, pRet->getEntrustID(), "");
	if (usertag1.empty())
	{
		pRet->setUserTag(pRet->getEntrustID());
	}
	else
	{
		pRet->setUserTag(usertag1.c_str());

		if (strlen(pRet->getOrderID()) > 0)
		{
			m_iniHelper.writeString(ORDER_SECTION, StrUtil::trim(pRet->getOrderID()).c_str(), usertag1.c_str());
			m_iniHelper.save();
			write_log(m_bscSink, LL_INFO, "[makeOrderInfo][{}][m_iniHelper]saving order_section: orderid: {}, usertag: {}", orderField->UserID, pRet->getOrderID(), usertag1);
		}
	}

	write_log(m_bscSink, LL_INFO, "[makeOrderInfo] orderstatus: {}, orderid: {}, usertag: {}, entrustid: {}, init orderref: {}", orderField->OrderStatus, pRet->getOrderID(), usertag1, pRet->getEntrustID(), orderField->OrderRef);

	return pRet;
}

WTSEntrust* TraderAresClt::makeEntrust(tagXTReqOrderInsertField *entrustField)
{
	if (entrustField == NULL)
	{
		write_log(m_bscSink, LL_ERROR, "[makeEntrust][tagXTReqOrderInsertField] entrustField is NULL!");
		return NULL;
	}

	WTSContractInfo* ct = m_bdMgr->getContract(entrustField->Code, entrustField->Exchange);
	if (ct == NULL)
	{
		write_log(m_bscSink, LL_ERROR, "[makeEntrust][tagXTReqOrderInsertField] Contract is NULL! Code: {}, OrderRef: {}", entrustField->Code, entrustField->OrderRef);
		return NULL;
	}

	WTSEntrust* pRet = WTSEntrust::create(
		entrustField->Code,
		entrustField->VolumeOrigin,
		entrustField->LimitPrice,
		ct->getExchg());

	pRet->setContractInfo(ct);
	pRet->setDirection(wrapDirectionType(entrustField->Direction[0], entrustField->Offset[0]));
	pRet->setPriceType(wrapPriceType(entrustField->PriceType[0]));
	pRet->setOffsetType(wrapOffsetType(entrustField->Offset[0]));

	pRet->setOrderFlag(WOF_NOR);

	//pRet->setEntrustID(generateEntrustID(m_frontID, m_sessionID, atoi(entrustField->OrderRef)).c_str());
	std::string tmp = entrustField->OrderRef;

	uint32_t ul;
	char * endptr;
	ul = strtoul(entrustField->OrderRef, &endptr, 10);
	generateEntrustID(pRet->getEntrustID(), ul);
	//write_log(m_bscSink, LL_INFO, "[makeEntrust] raw orderref: {}, strtoul ordref: {}", tmp, ul);

	//StringMap::iterator it = m_mapEntrustTag.find(pRet->getEntrustID());
	//if (it != m_mapEntrustTag.end())
	//{
	//	pRet->setUserTag(it->second.c_str());
	//}

	std::string usertag = m_iniHelper.readString(ENTRUST_SECTION, pRet->getEntrustID());
	//std::string usertag = m_eidCache.get(pRet->getEntrustID());
	if (!usertag.empty())
		pRet->setUserTag(usertag.c_str());

	write_log(m_bscSink, LL_INFO, "[makeEnturst][tagXTReqOrderInsertField]code: {}, entrustid: {}, usertag: {}, init orderref: {}", pRet->getCode(), pRet->getEntrustID(), pRet->getUserTag(), entrustField->OrderRef);

	return pRet;
}

WTSEntrust* TraderAresClt::makeEntrust(tagXTReqOrderCancelField *entrustField)
{
	if (entrustField == NULL)
	{
		write_log(m_bscSink, LL_ERROR, "[makeEntrust][tagXTReqOrderCancelField] entrustField is NULL!");
		return NULL;
	}

	WTSContractInfo* ct = m_bdMgr->getContract(entrustField->Code, entrustField->Exchange);
	if (ct == NULL)
	{
		write_log(m_bscSink, LL_ERROR, "[makeEntrust][tagXTReqOrderCancelField] Contract is NULL! Code: {}, OrderSysID: {}", entrustField->Code, entrustField->OrderSysID);
		return NULL;
	}

	WTSEntrust* pRet = WTSEntrust::create(
		entrustField->Code,
		0,
		0,
		ct->getExchg());

	pRet->setContractInfo(ct);

	generateEntrustID(pRet->getEntrustID(), atoi(entrustField->OrderSysID));

	//StringMap::iterator it = m_mapEntrustTag.find(pRet->getEntrustID());
	//if (it != m_mapEntrustTag.end())
	//{
	//	pRet->setUserTag(it->second.c_str());
	//}

	std::string usertag = m_iniHelper.readString(ENTRUST_SECTION, pRet->getEntrustID());
	//const char* usertag = m_eidCache.get(pRet->getEntrustID());
	if (!usertag.empty())
		pRet->setUserTag(usertag.c_str());

	write_log(m_bscSink, LL_INFO, "[makeEnturst][tagXTReqOrderCancelField][{}]code: {}, entrustid: {}, usertag: {}", entrustField->UserID, pRet->getCode(), pRet->getEntrustID(), pRet->getUserTag());

	return pRet;
}

WTSError* TraderAresClt::makeError(tagXTRspInfoField* rspInfo)
{
	WTSError* pRet = WTSError::create((WTSErroCode)rspInfo->ErrorID, rspInfo->ErrorMsg);
	return pRet;
}

WTSError* TraderAresClt::makeError(tagXTRspInfoField* rspInfo, WTSErroCode ec)
{
	WTSError* pRet = WTSError::create(ec, rspInfo->ErrorMsg);
	return pRet;
}

WTSTradeInfo* TraderAresClt::makeTradeRecord(tagXTTradeField *tradeField)
{
	if (tradeField == NULL)
		return NULL;

	WTSContractInfo* contract = m_bdMgr->getContract(tradeField->Code, tradeField->Exchange);
	if (contract == NULL)
		return NULL;

	write_log(m_bscSink, LL_INFO, "[OnRtnTrade] Code: {}, OrderRef: {}, Vol: {}, Price: {}, Time: {}, TrdID: {}, OrdSysID: {}",
		tradeField->Code, tradeField->OrderRef, tradeField->Volume, tradeField->Price, tradeField->TradeTime, tradeField->TradeID, tradeField->OrderSysID);

	WTSCommodityInfo* commInfo = contract->getCommInfo();

	WTSTradeInfo *pRet = WTSTradeInfo::create(tradeField->Code, commInfo->getExchg());
	pRet->setVolume(tradeField->Volume);
	pRet->setPrice(tradeField->Price);
	pRet->setTradeID(tradeField->TradeID);
	pRet->setContractInfo(contract);

	uint32_t uTime = tradeField->TradeTime;
	uint32_t uDate = tradeField->TradeDate;

	//if(uDate == m_pContractMgr->getTradingDate())
	//{
	//	//如果当前日期和交易日一致,且时间大于21点,说明是夜盘,也就是实际日期要单独计算
	//	if (uTime / 10000 >= 21)
	//	{
	//		uDate = m_pMarketMgr->getPrevTDate(commInfo->getExchg(), uDate, 1);
	//	}
	//	else if(uTime <= 3)
	//	{
	//		//如果在3点以内,就要先获取上一个交易日,再获取下一个自然日
	//		//这样做的目的是,遇到周五晚上的情况,可以处理过来
	//		uDate = m_pMarketMgr->getPrevTDate(commInfo->getExchg(), uDate, 1);
	//		uDate = TimeUtils::getNextDate(uDate);
	//	}
	//}

	pRet->setTradeDate(uDate);
	pRet->setTradeTime(TimeUtils::makeTime(uDate, uTime * 1000));

	WTSDirectionType dType = wrapDirectionType(tradeField->Direction[0], tradeField->Offset[0]);

	pRet->setDirection(dType);
	pRet->setOffsetType(wrapOffsetType(tradeField->Offset[0]));
	pRet->setRefOrder(tradeField->OrderSysID);
	//pRet->setTradeType((WTSTradeType)tradeField->TradeType);

	double amount = commInfo->getVolScale() * tradeField->Volume * pRet->getPrice();
	pRet->setAmount(amount);

	//StringMap::iterator it = m_mapOrderTag.find(pRet->getRefOrder());
	//if (it != m_mapOrderTag.end())
	//{
	//	pRet->setUserTag(it->second.c_str());
	//}

	if(strlen(tradeField->OrderRef) > 0)
	{
		thread_local char entrustid[64] = { 0 };
		generateEntrustID(entrustid, atoi(tradeField->OrderRef));
		std::string usertag = m_iniHelper.readString(ENTRUST_SECTION, entrustid);
		if (!usertag.empty())
			pRet->setUserTag(usertag.c_str());
	}
	else
	{
		std::string usertag = m_iniHelper.readString(ORDER_SECTION, StrUtil::trim(pRet->getRefOrder()).c_str());
		//std::string usertag = m_oidCache.get(StrUtil::trim(pRet->getRefOrder()).c_str());
		if (!usertag.empty())
			pRet->setUserTag(usertag.c_str());
	}
	

	write_log(m_bscSink, LL_INFO, "[Trade Record] code {} | price {} | volume {} | trade ordersysid {} | set reforder {} | usertag {}", pRet->getCode(), pRet->getPrice(), pRet->getVolume(), tradeField->OrderSysID, pRet->getRefOrder(), pRet->getUserTag());

	return pRet;
}

void TraderAresClt::generateEntrustID(char* buffer, uint32_t orderRef)
{
	fmtutil::format_to(buffer, "{}#{}#{}", m_strUserID, m_nLine, orderRef);
	write_log(m_bscSink, LL_INFO, "[generateEntrustID]generate entrustid: {}, orderref: {}", buffer, orderRef);
}

bool TraderAresClt::extractEntrustID(const char* entrustid, uint32_t &orderRef)
{
	auto idx = StrUtil::findLast(entrustid, '#');
	if (idx == std::string::npos)
		return false;

	orderRef = strtoul(entrustid + idx + 1, NULL, 10);

	/*thread_local static char buffer[64];
	wt_strcpy(buffer, entrustid);
	char* s = buffer;

	auto idx = StrUtil::findLast(entrustid, '#');
	if (idx == std::string::npos)
		return false;

	orderRef = strtoul(entrustid + idx + 1, NULL, 10);*/

	write_log(m_bscSink, LL_INFO, "[extractEntrustID]entrust id: {}, extract entrust id: {}", entrustid, orderRef);

	return true;
}

void TraderAresClt::OnErrRtnOrderInsert(tagXTReqOrderInsertField *pInputOrder, tagXTRspInfoField *pRspInfo)
{
	if (pRspInfo)
		m_strErrInfo = pRspInfo->ErrorMsg;

	WTSEntrust* entrust = makeEntrust(pInputOrder);
	if (entrust)
	{
		WTSError *err = makeError(pRspInfo, WEC_ORDERINSERT);
		if (m_bscSink)
			m_bscSink->onRspEntrust(entrust, err);

		//if (m_optSink)
		//	m_optSink->onRspEntrustOpt(entrust, err);

		entrust->release();
		err->release();
	}
}

bool TraderAresClt::isConnected()
{
	return (m_wrapperState == WS_ALLREADY);
}

int TraderAresClt::queryConfirm()
{
	if (m_pUserAPI == NULL || m_wrapperState != WS_LOGINED)
	{
		return -1;
	}

	return 0;
}

void TraderAresClt::FreeApi(ITDErebusApi* p)
{
	if (m_pfnDestroyObj)
		m_pfnDestroyObj(p);
}

void TraderAresClt::FreeApp(ITDErebusApp* p)
{
	if (m_pfnDestroyObj)
		m_pfnDestroyObj(p);
}
