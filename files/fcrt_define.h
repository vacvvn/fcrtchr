/**
 * @file fcrt_define.h
 * @author Нелюбин Виктор. ЗАО НТЦ Модуль (v.nelyubin@module.ru)
 * @brief определения для контроллера ВСРВ(ГРЭК)
 * @version 0.1
 * @date 2022-06-01
 * 
 * 
 */
#ifndef FCRT_DEF_H
#define FCRT_DEF_H

///размер области ОЗУ для внутреннего исп-я контроллера
/// @todo расчитывать по параметрам загрузочной таблицы
#define FCRT_INTERNAL_RAM_SZ_B 0x100000u
/// работать только через линию А (основную)
#define FCRT_FLAG_PHY_A     0x000000001u   
/// работать только через линию Б (резервную)
#define FCRT_FLAG_PHY_B     0x000000002u   


///выравнивание размера приемного/передающего буфера
#define FCRT_RXTX_BUF_SZ_ALIGN	64u

///размер доп.дескриптора для каждого приемного/передающего буфера
#define FCRT_BUF_DESC_SZ_B		64u

///маска адресов дма для контроллера
#define FCRT_DMA_MASK			0xFFFFFFFFull

/**
 * @brief выделяет блок памяти с указанным выравниванием
 * 
 */
typedef void* (*fcrt_allocator)(uint32_t, unsigned int, dma_addr_t*);

/**
 * @brief освобождает блок памяти, полученный fcrt_allocator
 * 
 */
typedef void (*fcrt_release)(void*);

/// структура описания канала передатчика
typedef struct {
	/// приоритет от 0 до 15
    unsigned priority;      
	/// период отправки сообщений в мкс
    unsigned period;        
	/// ASM ID
    unsigned asm_id;        
	/// максимальный размер сообщения в байтах
    unsigned max_size;      
	/// размер очереди сообщений
    unsigned q_depth;       
	/// флаги FCRT_FLAG_
    unsigned flags;         
} FCRT_TX_DESC;

/// структура описания канала приемника
typedef struct {
	/// ASM ID
    unsigned asm_id;        
	/// максимальный размер сообщения в байтах
    unsigned max_size;      
	/// размер очереди сообщений
    unsigned q_depth;       
	/// флаги FCRT_FLAG_
    unsigned flags;         
} FCRT_RX_DESC;

typedef struct 
{
	void __iomem * regs;
	FCRT_TX_DESC * txd;
	unsigned int nTx;
	FCRT_RX_DESC * rxd;
	unsigned int nRx;
	fcrt_allocator fcrt_alloc;
	fcrt_release fcrt_free;
	struct device * dev;
}FCRT_INIT_PARAMS;
/*
Для каждого ВК выделяется непрерывная память под буфера, при этом размер буфера
выравнивается на 64. Число буферов в канале определяется полем q_depth. Также выделятся
память под дескрипторы контроллера - по 64 байта на каждый буфер.
*/


/**
 * @brief иниц-я контроллера FCRT
 * @warning Массивы приемника-передатчика должны быть отсортированы по asm_id
 * 
 * @return int 0-OK;  EINVAL- неверный параметр; ENOMEM - ошибка при выделении памяти
 */
int fcrtInit(FCRT_INIT_PARAMS * param);
/**
 * @brief функция отправки сообщения по ВК
 * 
 * @param vc - номер ВК (индекс в массиве tx)
 * @param buf- адрес буфера, где лежит сообщение. 
 * @param size - размер сообщения в байтах
 * @return int 0-OK; EINVAL - задан неверный параметр; EAGAIN - в очереди нет места
 */
int fcrtSend(unsigned int vc, void* buf, unsigned int size);

/**
 * @brief принимает очередное сообщение из заданного ВК
 * 
 * @param vc номер ВК (индекс в массиве tx)
 * @param buf адрес буфера, куда надо положить сообщение
 * @param size указатель на размер принятого сообщения в байтах
 * @return int 0-OK; EINVAL задан неверный параметр; EAGAIN в очереди нет нового сообщения
 */
int fcrtRecv(unsigned int vc, void* buf, unsigned int * size);

/**
 * @brief проверка сообщений в очереди канала
 * 
 * @param vc - asm_id канала
 * @return int 0-сообщения есть; -EAGAIN - сообщений нет
 */
int fcrtChk(unsigned int vc);

/**
 * @brief освобождает ресурсы
 * 
 */
#endif