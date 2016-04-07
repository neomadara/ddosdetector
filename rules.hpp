#ifndef RULES_HPP
#define RULES_HPP

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

#include <boost/thread.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/format.hpp>

// Logging
#include "log4cpp/Category.hh"
#include "log4cpp/Priority.hh"

#include "exceptions.hpp"
#include "parser.hpp"
#include "lib/queue.hpp"
#include "action.hpp"
#include "functions.hpp"

// protocols
#include "ip.hpp"
#include "tcp.hpp"
#include "udp.hpp"
#include "icmp.hpp"

// Get log4cpp logger from main programm
extern log4cpp::Category& logger;

/*
 Класс для хранения правил анализа пакетов одного L4 протокола (TCP,
 UDP, ICMP и т.д.).
*/
template<class T>
class RulesList
{
public:
    explicit RulesList(boost::program_options::options_description opt);
    bool operator==(RulesList const & other)  const;
    /*
     сравнение списка. В стравнение проверяется только rules_ и last_update_
    */
    RulesList& operator=(const RulesList& other);
    /*
     сложение счетчиков правил. Используется потоком watcher для сбора
     статистики с различных копий листов в разных потоках. После сложения
     счетчиков, в other счетчики обнуляются.
    */
    RulesList& operator+=(RulesList& other);
    /*
     просчет delta величин: сколько пакетов/байтов получено за единицу
     времени (за 1 секунду).
     @param rules_old: лист правил со старыми данными (данными снятыми в
     прошлую итерацию); 
    */
    void calc_delta(const RulesList& rules_old);
    /*
     проверка триггеров правил, установка заданий обработчику заданий.
     @param task_list: ссылка на очередь заданий обработчика;
    */
    void check_triggers(ts_queue<action::TriggerJob>& task_list);
    /*
     добавление правила в конец листа
     @param rule: добавляемое правито типа T
    */
    void add_rule(T rule);
    /*
     удаление правила по номеру
     @param num: номер удаляемого правила в списке
    */
    void del_rule(const int& num);
    /*
     очистка списка правил
    */
    void clear();
    /*
     вставка нового правила в список на позицию num. Если позиция уже занята,
     все элементы, начиная с этой позиции сдвигаются к концу списка.
     @param num: позиция установки правила
     @param rule: добавляемое правило
    */
    void insert_rule(const int& num, T rule);
    /*
     проверка пакета по правилам листа. Функция вызывается для каждого
     полученного пакета T типа.
     @param l4header: пакет с обрезанными ip и ethernet заголовками
     @param s_addr: ip адрес источника
     @param d_addr: ip адрес назначения
     @param len: размер пакета целиком (с ip и ethernet заголовками)
    */
    template<typename H>
    void check_list(const H& l4header, const uint32_t& s_addr,
                    const uint32_t& d_addr, const unsigned int& len)
    {
        boost::lock_guard<boost::shared_mutex> guard(m_);
        for(auto& r: rules_)
        {
            if(r.check_packet(l4header, s_addr, d_addr))
            {
                r.count_packets++;
                r.count_bytes += len;
                if(!r.next_rule)
                {
                    break;
                }
            }
        }
    }
    /*
     вывод текстового представления правил и статистики.
    */
    std::string get_rules();
    /*
     возвращает параметры парсинга правил (переменная parse_opt_).
    */
    boost::program_options::options_description get_params() const;
private:
    mutable boost::shared_mutex m_;
    std::vector<T> rules_; // вектор для хранения правил
    boost::program_options::options_description parse_opt_; // опции парсинга правил
    std::chrono::high_resolution_clock::time_point last_update_; // время последнего изменения данных в листе (изменение счетчиков)
};

/*
 Класс для хранения листов правил для разных протоколов. Содержит методы
 для работы со всеми листами сразу. Доступ к определенному листу осуществляется
 на прямую, в виду чего классу не требуется дополнительной синхронизации между
 потоками (защита данных от инвариантности происходит на более гранулированном
 уровне - в классах RulesList).
*/
class RulesCollection
{
public:
    RulesCollection(boost::program_options::options_description& help_opt,
                boost::program_options::options_description& tcp_opt,
                boost::program_options::options_description& udp_opt,
                boost::program_options::options_description& icmp_opt);
    RulesCollection(const RulesCollection& parent, bool clear = false);
    bool operator!=(const RulesCollection& other) const;
    RulesCollection& operator=(const RulesCollection& other);
    RulesCollection& operator+=(RulesCollection& other);
    /*
     формирует справку по всем параметрам всех типов правил (переменная help_opt).
    */
    std::string get_help() const;
    /*
     формирует текстовое представление всех листов правил (вызываются функции
     RulesList<T>.get_rules())
    */
    std::string get_rules();
    /*
     проверяется допустим ли тип списка правил.
     @param type: название типа
    */
    bool is_type(const std::string& type) const;
    /*
     подсчет delta данных во всех списках
     @param old: старая версия данных
    */
    void calc_delta(const RulesCollection& old);
    /*
     проверка триггеров во всех списках правил
     @param task_list
    */
    void check_triggers(ts_queue<action::TriggerJob>& task_list);
private:
    std::vector<std::string> types_;
    boost::program_options::options_description help_;
public:
    RulesList<TcpRule> tcp; // лист правил для TCP
    RulesList<UdpRule> udp; // лист правил для UDP
    RulesList<IcmpRule> icmp; // лист правил для ICMP
};

/*
 класс загрузки/сохраненеи файла с правилами. Устанавливае signal_hook для перезагрузки
 конфигурации при получении сигнала SIGHUP.
*/
class RulesFileLoader
{
public:
    RulesFileLoader(boost::asio::io_service& service, const std::string& file,
        std::shared_ptr<RulesCollection>& c);
    /*
     загрука данных из файла правил, установка signal_hook
    */
    void start();
private:
    boost::asio::signal_set sig_set_;
    std::string rules_config_file_;
    std::shared_ptr<RulesCollection>& collect_;
    /*
     функция чтения данных из файла rules_config_file_.
     Читает данные и вносит правила в коллекцию collect_.
    */
    void reload_config();
    /*
     асинхронный обработчик сигнала SIGHUP, вызывает функцию чтения данных
     из конфига reload_config()
    */
    void sig_hook(boost::asio::signal_set& this_set_, boost::system::error_code error,
        int signal_number);
};

#endif // end RULES_HPP