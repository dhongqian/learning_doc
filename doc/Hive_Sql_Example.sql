-- 示例一：
-- 在一个表中，按不同条件选取相同的项：p2pflux、cdnflux、dcacheflux，再计算比值
-- 这其中使用到了 +、-、/
-- 函数用到了if
-- 如： sum( if( condition , p2pflux, 0)) as p2p_total_flux
-- if中，condition为true, 选p2pflux, 为flase,选0
-- sum中，只将condition为true的项进行了相加，为flase项，因为值为0，所以相当于未相加

SELECT
  (`p2p_total_flux` - `dcache_total_flux`) /(`p2p_total_flux` + `cdn_total_flux`) as t_rate,
  `p2p_total_flux` /(`p2p_total_flux` + `cdn_total_flux`) as t_rate_ex,
  (`p2p_wifi_flux` - `dcache_wifi_flux`) /(`p2p_wifi_flux` + `cdn_wifi_flux`) as w_rate,
  `p2p_wifi_flux` /(`p2p_wifi_flux` + `cdn_wifi_flux`) as w_rate_ex,
  (`p2p_net_flux` - `dcache_net_flux`) /(`p2p_net_flux` + `cdn_net_flux`) as n_rate,
  `p2p_net_flux` /(`p2p_net_flux` + `cdn_net_flux`) as n_rate_ex,
  p2p_total_flux,
  cdn_total_flux,
  dcache_total_flux,
  p2p_wifi_flux,
  cdn_wifi_flux,
  dcache_wifi_flux,
  p2p_net_flux,
  cdn_net_flux,
  dcache_net_flux,
  dt,
  hour,
  v,
  nat
FROM
  (
    SELECT
      sum(p2pflux) as p2p_total_flux,
      sum(cdnflux) as cdn_total_flux,
      sum(dcacheflux) as dcache_total_flux,
      sum(
        if(
          NOT(
            ua LIKE '%2G%'
            or ua LIKE '%3G%'
            or ua LIKE '%4G%'
            or ua LIKE '%5G%'
          ),
          p2pflux,
          0
        )
      ) as p2p_wifi_flux,
      sum(
        if(
          NOT(
            ua LIKE '%2G%'
            or ua LIKE '%3G%'
            or ua LIKE '%4G%'
            or ua LIKE '%5G%'
          ),
          cdnflux,
          0
        )
      ) as cdn_wifi_flux,
      sum(
        if(
          NOT(
            ua LIKE '%2G%'
            or ua LIKE '%3G%'
            or ua LIKE '%4G%'
            or ua LIKE '%5G%'
          ),
          dcacheflux,
          0
        )
      ) as dcache_wifi_flux,
      sum(
        if(
          ua LIKE '%2G%'
          or ua LIKE '%3G%'
          or ua LIKE '%4G%'
          or ua LIKE '%5G%',
          p2pflux,
          0
        )
      ) as p2p_net_flux,
      sum(
        if(
          ua LIKE '%2G%'
          or ua LIKE '%3G%'
          or ua LIKE '%4G%'
          or ua LIKE '%5G%',
          cdnflux,
          0
        )
      ) as cdn_net_flux,
      sum(
        if(
          ua LIKE '%2G%'
          or ua LIKE '%3G%'
          or ua LIKE '%4G%'
          or ua LIKE '%5G%',
          dcacheflux,
          0
        )
      ) as dcache_net_flux,
      dt,
      hour,
      v,
      nat
    FROM
      `bi_warehouse_dwd`.`dwd_hh_fact_view_qos_p2p_task_8_82_820`
    WHERE
      (
        ua LIKE '%2G%'
        or ua LIKE '%3G%'
        or ua LIKE '%4G%'
        or ua LIKE '%5G%'
      )
      and v = '10.1.2.3704'
      and dt = '2021-07-08'
	  and hour = "21"
    group by
      dt,
      hour,
      v,
      nat
  ) outer
LIMIT
  1000