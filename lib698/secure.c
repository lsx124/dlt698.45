/*
 * secure.c
 *
 *  Created on: 2017-1-11
 *      Author: gk
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "ParaDef.h"
#include "secure.h"
#include "Shmem.h"
#include "Esam.h"
/**********************************************************************
 *安全传输中获取应用数据单元长度
 *参见A-XDR编码规则(第一个字节最高bit位代表是否可变长度，0代表本身即长度字节，不超128,1代表剩余bit是长度域字节数，先取长度域再取长度)
 *输入：apdu完整的应用数据单元开头地址
 *输出：应用数据单元长度包括长度的1或2个字节或3个字节
 **********************************************************************/
extern INT8U securetype;//dlt698.c定义
extern INT8U secureRN[20];
extern ProgramInfo *memp;
 INT16U GetDataLength(INT8U* Data)
 {
	 INT16U datalen=0;
		if((Data[0] &0x80) != 0x80)//最高位代表长度域的属性
		{
			datalen=Data[0]+1;
		}
		else
		{
			if((Data[0] &0x7F) == 0x01)//长度域1个字节长度
				datalen=Data[1]+2;
			else if((Data[0] &0x7F) == 0x02)//长度域2个字节长度
				datalen=((INT16U)Data[1]<<8)+(INT16U)Data[2] +3 ;//
			else datalen=0;//长度域不会超过2个字节，超过的话此处做异常处理
		}
		if(datalen > BUFLEN) datalen=0;//超过上行最大长度，做异常处理
		return datalen;
 }
 //给出字符串长度，返回长度字节数和对应的字节(用于组octet-string字符串)
 //如 96 返回1，retData=0x60,    如130 返回2，retData = 0x81,0x82  如258 返回2，retData = 0x82 0x01,0x02
 //处理长度最大用2个字节可表示长度
 INT16U GetLengthByte(INT16U Length,INT8U *retData)
 {
	 if(Length>0 && Length<128)
	 {
		 retData[0]=Length;
		 return 1;
	 }
	 else if(Length>=128 && Length<256)
	 {
		 retData[0]=0x81;
		 retData[1]=Length;
		 return 2;
	 }
	 else if(Length>=256)
	 {
		 retData[0]=0x82;
		 retData[1]=Length/256;
		 retData[2]=Length%256;
		 return 3;
	 }
	 else
		 return 0;
 }
 /**********************************************************************
  *建立应用链接（）
  *输入：SignatureSecurity为主站下发解析得到；SecurityData为上行报文结构数据，需填充
  *输出：返回正值正确，否则失败返回-20X，与esam.C中返回的兼容，防止返回重复错误标志
  **********************************************************************/
 //已测/但测试报文无法通过校验，应该是esam芯片内证书和报文证书不匹配
INT32S secureConnectRequest(SignatureSecurity* securityInfo ,SecurityData* RetInfo)
{
	 if(securityInfo->encrypted_code2[0] == 0x00 || securityInfo->signature[0]==0x00)
		  return -201;
    // fprintf(stderr,"secureConnectRequest  securityInfo= %d   =%d  \n",securityInfo->encrypted_code2[0],securityInfo->signature[0]);
     INT32S ret= Esam_CreateConnect( securityInfo , RetInfo);
     return ret;
}
/**********************************************************************
*处理主动上报，主站回复报文同esam交互部分(按照宣贯资料来操作，需要RN。芯片手册需要MAC)
 **********************************************************************/
INT32S secureResponseData(INT8U* RN,INT8U* apdu)
{
	 INT32S ret=0;
	 INT16S MACindex=0;//MAC所在位置可能因为应用数据单元长度不定而变化
    INT16S len = GetDataLength(&apdu[2]);

    if(len>255)
    	MACindex=2+2+len;
    else if(len>0 && len<256)
    	MACindex=2+1+len;
    else
    		return -202;
    ret =Esam_DencryptReport(  RN, &apdu[MACindex], &apdu[2], apdu);
     return ret;
}
/**********************************************************************
 *单位解析，解析安全传输中SID,MAC,RN
 *输入：type定义的数据类型（下发报文不含数据类型标示）0x01  SID  0x02 RN MAC
 *输出：source解析长度
 *说明：type=0x01时dest是SID类型结构体；RN/MAC/SID中随机数，开头一个字节都是长度
 **********************************************************************/
INT32S UnitParse(INT8U* source,INT8U* dest,INT8U type)
{
	INT32S len=0;
	if(type==0x01)//SID
	{
		memcpy(dest,source,4);//标识double-long-unsigned
		dest+=4;
		source+=4;
		if(source[0]!=0)
			memcpy(dest,source,source[0]+1);//附加信息（包含一个字节长度）
		len=len+4+1+source[0];
	}
	else if(type == 0x02)//RN  OR MAC    octet-string类型（包含一个字节长度）
	{
		if(source[0]!=0)
			memcpy(dest,source,source[0]+1);
		len+=source[0]+1;
	}
	else
		return -201;
	return len;
}
//esam芯片手册：5.3第三部分对组地址和广播进行安全验证
//安全标示最后4个bit的校验
INT32S secureBroadcastCheck(SID_MAC *sid_mac,CSINFO *csinfo)
{
	INT8U Check[3] = {0x80,0x16,0x48};
	if(memcmp(Check,sid_mac->sid.sig,3)!=0)
		return 0;
	if((sid_mac->sid.sig[3]&0xF0)!=0x00)
		return 0;
	INT8U lastAddr = csinfo->sa[csinfo->sa_length];//此处0-15，代表1-16，所以不需要-1
	if((lastAddr &0x0F) == 0x0F)
		lastAddr = lastAddr << 4;
	else
		lastAddr = lastAddr&0x0F;
	if(lastAddr ==0x00)
		lastAddr = 0x0A;
	if(lastAddr>=0x00 && lastAddr<=0x0A)
	{
		sid_mac->sid.sig[3] = (sid_mac->sid.sig[3]&0xF0) | lastAddr;//清掉原先的底四位，再补上底四位
		return 0;
	}
	else
		return -1;
}
 /**********************************************************************
  *应用数据单元为密文情况时处理方案（ 698解析应用数据单元和数据验证信息.访问模型参见4.1.3.1）
  *当前理解：密文+SID为  《密文》等级   密文+SID_MAC为《密文+MAC》等级，密文情况下不存在RN/RN_MAC情况
  *因为密文+RN在安全芯片手册中找不到解密方法
  *输入：需返回的secureType安全类别，01明文，02明文+MAC 03密文  04密文+MAC
   *输入：CSINFO用来在组地址或广播地址时对安全标示验证（esam芯片手册：5.3第三部分）
  *输出：retData长度
  **********************************************************************/
 INT32S secureEncryptDataDeal(INT8U* apdu,INT8U* retData,CSINFO *csinfo)
 {
	 INT32S tmplen=0;
	 INT16U appLen=0;
	 INT32S ret=0;
	 SID_MAC sidmac;
	 memset(&sidmac,0,sizeof(SID_MAC));
	 appLen = GetDataLength(&apdu[2]);
	 if(appLen<=0) return -201;
	if(apdu[2+appLen]==0x00 ||apdu[2+appLen]==0x03)//SID_MAC数据验证码
	{
		tmplen = UnitParse(&apdu[2+appLen+1],(INT8U*)&sidmac,0x01);//解析SID部分
		if(tmplen<=0) return -202;
		//判断服务器地址类型是否为组地址或广播地址
		if(csinfo->sa_type==0x02 ||csinfo->sa_type==0x03 )
		{
			if(secureBroadcastCheck(&sidmac,csinfo)<0)
				return -203;
		}
		if(apdu[2+appLen]==0x00)
		{
			tmplen = UnitParse(&apdu[2+appLen+1+tmplen],sidmac.mac,0x02);//解析MAC部分
			if(tmplen<=0) return -204;//
		}
		ret = Esam_SIDTerminalCheck(sidmac,&apdu[2],retData);
	}
	else
		return -205;
	if(apdu[2+appLen]==0x00)
		securetype=0x04;//密文+mac等级
	if(apdu[2+appLen]==0x03)
		securetype=0x03;//密文等级
	return ret;
 }
 /**********************************************************************
  *应用数据单元为明文情况时处理方案
  *当前理解：当前资料主要以明文+RN为主，为后续，兼容明文+RN_MAC情况。
  *输入：返回安全类别，此处类别总是02明文+MAC
  *mac值传入上层函数，暂时用不到
  *输出：retData长度
  **********************************************************************/
 INT32S secureDecryptDataDeal(INT8U* apdu)
 {
	 INT32S ret=0;
	 INT16U appLen = GetDataLength(&apdu[2]);//计算应用数据单元长度(包括长度字符)
	 INT8U tmpbuff[2048];
	 memset(tmpbuff,0,2048);
	 if(appLen<=0) return -201;
	 securetype=0x02;//明文+MAC等级
	 if(apdu[2+appLen]==0x01 || apdu[2+appLen]==0x02)// 只处理RN情况(RN_MAC情况，是主动上报)
	 {
		 if(apdu[2+appLen+1]<=sizeof(secureRN))//随机数字符数小于secureRN大小(原则是16byte)
		 {
			 memcpy(secureRN,&apdu[2+appLen+1],apdu[2+appLen+1]+1);//包括第一字节长度
			 ret=appLen;
		 }
	 }
	 else if(apdu[2+appLen]==0x00 || apdu[2+appLen]==0x03)//处理明文状态下，收到SID/SID_MAC异常情况
	 {
		 if(secureCheckDataSidMac(apdu,appLen)<=0)//校验mac失败，此报文不作解析
			 return -202;
		 ret=appLen;
	 }
	 else
		 return -203;
	 if(apdu[2]<0x80)//长度字节为1个字节
	 {
		 memcpy(tmpbuff,&apdu[3],appLen-1);
		 memcpy(apdu,tmpbuff,appLen-1);
		 ret=appLen-1;
	 }
	 else if(apdu[2]==0x81)//长度2个字节
	 {
			 memcpy(tmpbuff,&apdu[4],appLen-2);
			 memcpy(apdu,tmpbuff,appLen-2);
			 ret=appLen-2;
	 }
	 else if(apdu[2] == 0x82)//长度为3个字节
	 {
		 memcpy(tmpbuff,&apdu[5],appLen-3);
		 memcpy(apdu,tmpbuff,appLen-3);
		 ret=appLen-3;
	 }
	 else
		 return -204;

	 return ret;
 }
 //主站下发明文+sid/sid_mac时，需要将明文和sid放入esam芯片检验正确行
 //输入:apdu明文开始地址，appLen明文长度(包括长度字符，外面计算好，此处不再计算)
 //返回，正数即明文长度，因为esam返回是明文，主站下发的也是明文，直接使用主站下发明文，不再需要esam返回的数据，只作正确与否的判断
 INT32S secureCheckDataSidMac(INT8U* apdu, INT16U appLen)
 {
	 //apdu[2+appLen]位置，一定是00或03，外面已作判断
	 SID_MAC sidmac;
	 INT32S tmplen=0;
	 INT8U retData[2048];
	 memset(&sidmac,0,sizeof(SID_MAC));
	 memset(retData,0,2048);
	 tmplen = UnitParse(&apdu[2+appLen+1],(INT8U*)&sidmac,0x01);//解析SID部分
	 if(tmplen<=0) return -201;
	 if(apdu[2+appLen]==0x00)
		{
			tmplen = UnitParse(&apdu[2+appLen+1+tmplen],sidmac.mac,0x02);//解析MAC部分
			if(tmplen<=0) return -202;//
		}
	 tmplen = Esam_SIDTerminalCheck(sidmac,&apdu[2],retData);
	 return tmplen;
 }
 //获取ESAM主站/终端证书   证书都是大于1000字节，此处按照大于1000,2个字节组织上送报文
 	//ccieFlag证书标识  0x0C主站证书   0x0A终端证书
 	INT32S getEsamCcie(INT8U ccieFlag,INT8U *retBuff)//&&已测
 	{
 		INT32S retLen=0;
 		INT8U buff[2048];
 		memset(buff,0,2048);
 		if(ccieFlag==0x0C)	//主站证书
 			retLen = Esam_GetTermiSingleInfo(0x0C,buff);
 		else                        //终端证书
 			retLen=Esam_GetTermiSingleInfo(0x0B,buff);//此处芯片手册和属性编号不一致
 		if(retLen>10)   //10值为随意值，正常证书长度1500左右(正常状态下)
 		{
 			retBuff[0] = 0x09;//octet-string类型
 			retBuff[1] = 0x82;//可变长度，2个字节
 			retBuff[2] = (INT8U)((retLen>>8)&0x00ff);//长度字节高字节
 			retBuff[3] = (INT8U)(retLen&0x00ff);//长度字节低字节
 			memcpy(&retBuff[4],buff,retLen);
 			retLen+=4;
 		}
 		return retLen;
 	}

 //组织属性对象中的当前计数器，返回添加进入的字节数量
 //attrindex OAD内属性索引
 	//已测
 INT8U composeEsamCurrentCounter(INT8U attrindex,EsamInfo *esamInfo,INT8U *retBuff)
 {
	 INT8U retLen=0;
	 if(attrindex == 0x01)//单地址应用协商计数器
	 {
		 retBuff[0]=0x06;//double-long-unsigned
		 memcpy(&retBuff[1],esamInfo->CurrentCounter.SingleAddrCounter,4);
		 retLen=0x05;
	 }
	 else if(attrindex == 0x02)//主动上报计数器
	 {
		 retBuff[0]=0x06;//double-long-unsigned
		 memcpy(&retBuff[1],esamInfo->CurrentCounter.ReportCounter,4);
		 retLen=0x05;
	 }
	 else if(attrindex == 0x03)//应用广播通信序列号
	 {
		 retBuff[0]=0x06;//double-long-unsigned
		 memcpy(&retBuff[1],esamInfo->CurrentCounter.BroadCastSID,4);
		 retLen=0x05;
	 }
	 else//当前计数器整个信息域
	 {
		retBuff[0]=0x02;//struct
		retBuff[1]=0x03;//3个元素
		retBuff[2]=0x06;//double-long-unsigned
		memcpy(&retBuff[3],esamInfo->CurrentCounter.SingleAddrCounter,4);
		retBuff[7]=0x06;
		memcpy(&retBuff[8],esamInfo->CurrentCounter.ReportCounter,4);
		retBuff[12]=0x06;
		memcpy(&retBuff[13],esamInfo->CurrentCounter.BroadCastSID,4);
		retLen=2+3*5;
	 }
	 return retLen;
 }
 //ESAM证书版本
 INT8U composeEsamCcieVersion(INT8U attrindex,EsamInfo *esamInfo,INT8U *retBuff)
 {
	 INT8U retLen=0;
	 if(attrindex == 0x01)
	 {
		 retBuff[0]=0x09;//octet-string
		 retBuff[1]=0x01;//长度
		 retBuff[2]=esamInfo->CcieVersion.TerminalCcieVersion;
		 retLen=3;
	 }
	 else if(attrindex ==0x02)
	 {
		 retBuff[0]=0x09;//octet-string
		 retBuff[1]=0x01;//长度
		 retBuff[2]=esamInfo->CcieVersion.ServerCcieVersion;
		 retLen=3;
	 }
	 else
	 {
		 retBuff[0]=0x02;//struct
		 retBuff[1]=0x02;//3个元素
		 retBuff[2]=0x09;//octet-stirng
		 retBuff[3]=0x01;//长度
		 retBuff[4]=esamInfo->CcieVersion.TerminalCcieVersion;
		 retBuff[5]=0x09;//octet-stirng
		 retBuff[6]=0x01;//长度
		 retBuff[7]=esamInfo->CcieVersion.ServerCcieVersion;
		 retLen=8;
	 }
	 return retLen;
 }
 //ESAM属性获取
 //输入：单个oad，retBuff,返回字符串指针，将需要返回字符串写入，开头为长度
 //输出：写入retBuff长度
 //当属性为0的时候，返回错误，因为无法将主站和客户端的证书组报文返回去，也没这么用的
 //返回值不能为负数
INT16U getEsamAttribute(OAD oad,INT8U *retBuff)
{
	INT32S retLen=0;
//	static struct timeval tv_store;//存储静态时间，用于list类型属性提取，不用多次esam访问
//	static 	EsamInfo esamInfo;//
	EsamInfo esamInfo;
	memset(&esamInfo,0,sizeof(EsamInfo));
	INT8U attnum = oad.attflg&0x1F;
	if(attnum == 0x0C || attnum==0x0A)//主站/终端证书属性
	{
		retLen = getEsamCcie(attnum,retBuff);
		return retLen<=0 ? 0:retLen;
	}
//	struct timeval tv_new;//静态存储时间
//	gettimeofday(&tv_new, NULL);
//	if(tv_store.tv_sec == 0 || (tv_new.tv_sec - tv_store.tv_sec)>=3)//第一次进入该函数，或有效时间超过3秒，重新esam访问
//	{
		retLen = Esam_GetTermiInfo(&esamInfo);
//		if(retLen>0)
//			memcpy(&tv_store,&tv_new,sizeof(tv_store));//更新存储时间
		//else return 0;
		if(retLen<=0) return 0;
//	}
	//经过以上的过滤，处理3种情况(第一次进入，时间超时，证书，一下直接从esamInfo中拷贝属性信息)
	switch(attnum)
	{
		case 0x02:    //ESAM序列号
			retBuff[0] = 0x09;//octet-string
			retBuff[1] = 0x08;//长度
			memcpy(&retBuff[2],esamInfo.EsamSID,8);
			retLen=2+8;
			break;
		case 0x03:   //ESAM版本号
			retBuff[0] = 0x09;//octet-string
			retBuff[1] = 0x04;
			memcpy(&retBuff[2],esamInfo.EsamVID,4);
			retLen=2+4;
			break;
		case 0x04:    //对称秘钥版本
			retBuff[0] = 0x09;//octet-string
			retBuff[1] = 0x10;
			memcpy(&retBuff[2],esamInfo.SecretKeyVersion,16);
			retLen=2+16;
			break;
		case 0x05:    //会话时效门限
			retBuff[0] = 0x06;//double-long-unsigned
//			retBuff[1] = 0x04;		//湖南测试招测ESAM信息去掉,该属性为 [类型+数据],不需要长度
			memcpy(&retBuff[2],esamInfo.SessionTimeHold,4);
			retLen=1+4;//retLen=2+4;
			break;
		case 0x06:   //会话时效剩余时间
			retBuff[0] = 0x06;//double-long-unsigned
//			retBuff[1] = 0x04;
			memcpy(&retBuff[2],esamInfo.SessionTimeLeft,4);
			retLen=1+4;//retLen=2+4;
			break;
		case 0x07:    //当前计数器
			retLen = composeEsamCurrentCounter(oad.attrindex,&esamInfo,retBuff);
			break;
		case 0x08:      //证书版本
			retLen = composeEsamCcieVersion(oad.attrindex,&esamInfo,retBuff);
			break;
		case 0x09:     //终端证书序列号
			retBuff[0] = 0x09;//octet-string
			retBuff[1] = 0x10;
			memcpy(&retBuff[2],esamInfo.TerminalCcieSID,16);
			retLen=2+16;
			break;
		case 0x0B:    //主站证书序列号
			retBuff[0] = 0x09;//octet-string
			retBuff[1] = 0x10;
			memcpy(&retBuff[2],esamInfo.ServerCcieSID,16);
			retLen=2+16;
			break;
		case 0x0D:    //ESAM安全存储对象列表
			break;
		default:
			break;
	}
	return retLen;
}
//每次进行密钥更新时，需要再读一下芯片信息，更新共享内存中Esam_VersionStatus状态，涉及液晶小房子
INT8S esam_UpdateShmemStatus()
{
	INT32S retLen=0;
	EsamInfo esamInfo;
	INT8U i=0;
	memset(&esamInfo,0,sizeof(EsamInfo));
	retLen = Esam_GetTermiInfo(&esamInfo);
	if(retLen<0) return -1;
	for(i=0;i<16;i++)
		if(esamInfo.SecretKeyVersion[i]!=0x00) return 1;//测试芯片16字节全为0
	return 0;
}
//esam方法操作7，秘钥更新（02 02 09 82 00 C0 7F CA 75。。。。）
//输入：Data2为原始报文头，包括真个秘钥更新的结构体  02结构体 02 2个元素 09 octetstring 82 可变2个字节 00 C0长度字节 7F CA 75。。。。
//输出：
INT32S esamMethodKeyUpdate(INT8U *Data2)
{
	 INT16U secureLen=0;
	 SID_MAC sidmac;
	 INT32S tmplen=0;
	 memset(&sidmac,0,sizeof(SID_MAC));
	if(Data2[0]==0x02 && Data2[1]==0x02 && Data2[2]==0x09)//此处必须严格遵守字节值
	{
		secureLen = GetDataLength(&Data2[3]);//包含头部的长度字节数量
		if(secureLen <= 0)			return -201;
		tmplen = UnitParse(&Data2[3+secureLen+1],(INT8U *)&sidmac,0x01);//填充sidmac中sid部分
		if(tmplen<=0)			return -202;
		tmplen = UnitParse(&Data2[3+secureLen+1+tmplen],sidmac.mac,0x02);//填充mac
		if(tmplen<=0)			return -203;
		tmplen = Esam_SymKeyUpdate(sidmac,&Data2[3]);//秘钥更新
		return tmplen;
	}
	else
		return -204;
}
//esam 方法操作8  证书更新----------//esam 方法操作9 设置协商时效门限
//方法8和9   统一一个方法解决
INT32S esamMethodCcieSession(INT8U *Data2)
{
	 INT16U secureLen=0;
	 SID sid;
	 memset(&sid,0,sizeof(SID));
	 INT32S tmplen=0;
	 if(Data2[0]==0x02 && Data2[1]==0x02 && Data2[2]==0x09)//此处必须严格遵守字节值
	{
		 secureLen = GetDataLength(&Data2[3]);//包含头部的长度字节数量
		if(secureLen <= 0)			return -201;
		tmplen = UnitParse(&Data2[3+secureLen+1],(INT8U *)&sid,0x01);//填充sidmac中sid部分
		if(tmplen<=0) 					return -202;
		tmplen = Esam_CcieSession(sid,&Data2[3]);//证书更新///协商时效门限
		return tmplen;
	}
	 else return -203;
}
//回复明文加mac   区分读取和其他
INT32S compose_DataAndMac(INT8U* SendApdu,INT16U Length)
{
	 INT8U esamBuff[2048];//送入esam，获取esam返回信息
	 memset(esamBuff,0,2048);
	 INT8U bytelen[3];
	 memset(bytelen,0,3);
	 INT8U BuffTmp[2048];
	 memset(BuffTmp,0,2048);
	 INT16U retLen=0;
	 INT32S esamRet=0;
	 BuffTmp[0]=0x90;//安全传输应答标识
	 if(SendApdu[0] == 133)//读取的上报
		 esamRet = Esam_GetTerminalInfo(secureRN,SendApdu,Length,esamBuff);
	 else
		 esamRet = Esam_SIDResponseCheck(0x11,SendApdu,Length,esamBuff);
	 if(esamRet>0)//正常返回4字节MAC
	 {
		 BuffTmp[1]=0x00;//明文传输标识
		 retLen = GetLengthByte(Length,bytelen);
		 memcpy(&BuffTmp[2],&bytelen[0],retLen);//复制长度字符串
		 memcpy(&BuffTmp[2+retLen],SendApdu,Length);//复制明文字符串
		 BuffTmp[2+retLen+Length]=0x01;//有MAC
		 BuffTmp[2+retLen+Length+1]=0x00;//MAC标识
		 BuffTmp[2+retLen+Length+2] = 0x04;//MAC长度4字节
		 if(SendApdu[0] == 133)//读取的上报
			 memcpy(&BuffTmp[2+retLen+Length+3],esamBuff,4);//复制mac(终端读取)
		 else
			 memcpy(&BuffTmp[2+retLen+Length+3],&esamBuff[esamRet-4],4);//非终端读取（esam返回的是明文加mac，此处需验证）
		 memcpy(SendApdu,BuffTmp,2+retLen+Length+7);//复制回SendApdu
		 return 2+retLen+Length+7;
	 }
	 else
		 return esamRet;
}
//回复密文
INT32S compose_EnData(INT8U* SendApdu,INT16U Length)
{
	 INT8U esamBuff[2048];//送入esam，获取esam返回信息
	 memset(esamBuff,0,2048);
	 INT8U bytelen[3];
	 memset(bytelen,0,3);
	 INT8U BuffTmp[2048];
	 memset(BuffTmp,0,2048);
	 INT32S esamret=0;
	 INT8U retLen=0;

	 BuffTmp[0]=0x90;//安全传输应答标识
	 esamret = Esam_SIDResponseCheck(0x96,SendApdu,Length,esamBuff);
	 fprintf(stderr,"\nesamret = %d",esamret);
	 if(esamret>0)//正常返回4字节MAC
	{
		 BuffTmp[1]=0x01;//密文传输标识
		 retLen = GetLengthByte(esamret,bytelen);
		 memcpy(&BuffTmp[2],&bytelen[0],retLen);//复制长度字符串
		 memcpy(&BuffTmp[2+retLen],esamBuff,esamret);//复制密文字符串
		 BuffTmp[2+retLen + esamret] = 0x00;//无mac

		 memcpy(SendApdu, BuffTmp, 2+retLen+esamret+1);

		 return 2+retLen+esamret+1;
	}
	 else
		 return esamret;
}
//回复密文+mac
INT32S compose_EnDataAndMac( INT8U* SendApdu,INT16U Length)
{
	 INT8U esamBuff[2048];//送入esam，获取esam返回信息
	 memset(esamBuff,0,2048);
	 INT8U bytelen[3];
	 memset(bytelen,0,3);
	 INT8U BuffTmp[2048];
	 memset(BuffTmp,0,2048);
	 INT32S esamret=0;
	 INT8U retLen=0;

	 BuffTmp[0]=0x90;//安全传输应答标识
	 esamret = Esam_SIDResponseCheck(0x97,SendApdu,Length,esamBuff);
	 if(esamret>0)//正常返回4字节MAC
	{
		 BuffTmp[1]=0x01;//密文传输标识
		 retLen = GetLengthByte(esamret-4,bytelen);//去除尾部4个字节mac，得到的密文长度
		 memcpy(&BuffTmp[2],&bytelen[0],retLen);//复制长度字符串
		 memcpy(&BuffTmp[2+retLen],esamBuff,esamret-4);//复制密文字符串
		 BuffTmp[2+retLen+esamret-4]=0x01;//有MAC
		 BuffTmp[2+retLen+esamret-4+1]=0x00;//MAC标识
		 BuffTmp[2+retLen+esamret-4+2] = 0x04;//mac长度
		 memcpy( &BuffTmp[2+retLen+esamret-4+3],&esamBuff[esamret-4],4);//复制mac
		 memcpy(SendApdu,BuffTmp,2+retLen+esamret-4+7);
		 return 2+retLen+esamret-4+7;
	}
	 else
		 return esamret;
}
//终端抄读电表时，组下行报文，部分数据项需要用明文加RN方式进行，该函数用来获取随机数RN
//RN 用以返回RN数据
//返回：正常返回rn长度，异常返回负数错误码
INT32S esamMeterGetRN(INT8U *RN)
{
	return Esam_GetRN(RN);
}
//终端抄读电表，上行报文解析
//分3种情况，明文/密文/密文+mac，具体参照esam函数详细说明
//输入：Esam_MAC_RN_NO将3种情况参数用一个结构体包括，具体使用用到谁填谁
//           apdu 明文/或密文，开头包括长度字节
//输出：rbuf，如果是密文，返回解密的明文
//返回值:明文的情况，返回正数代表解密正常。密文返回正数代码明文长度。负数的情况代码验证失败
INT32S esamMeterDataParse(Esam_MAC_RN_NO* InfoData, INT8U *apdu,INT8U* Rbuf)
{
	return  Esam_EmeterDataDencrypt(InfoData,apdu,Rbuf);
}
